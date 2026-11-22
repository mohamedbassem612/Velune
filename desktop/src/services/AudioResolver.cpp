#include "AudioResolver.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>
#include <utility>
namespace velune {
struct CachedSource {
    QUrl url;
    QJsonObject headers;
    qint64 duration = 0, expiry = 0;
};
static QHash<QString, CachedSource> sources;
static QString sourceKey(const QString &id, const QByteArray &cookies) {
    return id + ":" +
           QString::fromLatin1(QCryptographicHash::hash(cookies, QCryptographicHash::Sha256).toHex());
}
bool AudioResolver::matches(const QString &id, const QByteArray &cookies) const {
    return busy() && cacheKey == sourceKey(id, cookies);
}
void AudioResolver::publish(const QUrl &url, const QJsonObject &headers, qint64 duration, qint64 lifetime) {
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto expiry = QUrlQuery(url).queryItemValue("expire").toLongLong();
    if (expiry > 0)
        lifetime = qMin(lifetime, expiry - now - 60);
    for (auto i = sources.begin(); i != sources.end();)
        if (i->expiry <= now)
            i = sources.erase(i);
        else
            ++i;
    if (sources.size() >= 64)
        sources.erase(sources.begin());
    if (lifetime > 0)
        sources[cacheKey] = {url, headers, duration, now + qMin(lifetime, qint64(1800))};
    accountCookies.clear();
    emit ready(url, headers, duration);
}

QString AudioResolver::toolPath(const QString &name) {
#ifdef Q_OS_WIN
    const auto filename = name + ".exe";
#else
    const auto filename = name;
#endif
    const auto bundled = QCoreApplication::applicationDirPath() + "/tools/" + filename;
    if (QFileInfo(bundled).isExecutable())
        return bundled;
    return QStandardPaths::findExecutable(filename);
}
AudioResolver::AudioResolver(QObject *parent, QString binary, QString js, QNetworkAccessManager *transport)
    : QObject(parent), direct(this, transport), useDirect(binary.isEmpty() || transport),
      executable(binary.isEmpty() ? toolPath("yt-dlp") : binary),
      runtime(js.isEmpty() ? toolPath("deno") : js) {
    connect(&direct, &NativeAudioResolver::ready, this,
            [this](QUrl url, QJsonObject headers, qint64 duration, qint64 lifetime) {
                emit stage("native_resolver_completed");
                publish(url, headers, duration, lifetime);
            });
    connect(&direct, &NativeAudioResolver::unavailable, this, [this] { launch(true); });
    timeout.setSingleShot(true);
    timeout.setInterval(120000);
    connect(&timeout, &QTimer::timeout, this,
            [this] { failed("Preparing this song timed out. Check your connection and try again."); });
}
void AudioResolver::createProcess() {
    // Each launch owns a distinct process; retired processes cannot publish into a new request.
    if (process) {
        process->disconnect(this);
        process->deleteLater();
    }
    process = new QProcess(this);
    connect(process, &QProcess::readyReadStandardOutput, this, [this] {
        output += process->readAllStandardOutput();
        if (output.size() > 4 * 1024 * 1024)
            failed("The audio resolver returned an invalid response.");
    });
    connect(process, &QProcess::readyReadStandardError, this,
            [this] { diagnostic = (diagnostic + process->readAllStandardError()).right(16384); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            failed("The playback helper could not start. Reinstall Velune's playback tools.");
    });
    connect(
        process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            if (!work)
                return;
            timeout.stop();
            output += process->readAllStandardOutput();
            diagnostic = (diagnostic + process->readAllStandardError()).right(16384);
            // yt-dlp may update its cookie jar; remove it before returning audio to the player.
            QFile::remove(work->filePath("cookies.txt"));
            if (code != 0 || status != QProcess::NormalExit) {
                if (fastAttempt && status == QProcess::NormalExit) {
                    // Retry with complete extraction and the account session when needed.
                    work.reset();
                    output.clear();
                    diagnostic.clear();
                    launch(false);
                    return;
                }
                const auto detail = diagnostic.toLower();
                QString message = "YouTube could not prepare this song. Check your connection and try again.";
                if (detail.contains("sign in") || detail.contains("login") || detail.contains("cookies"))
                    message =
                        "YouTube needs a fresh account session for this song. Sign out, then sign in again.";
                else if (detail.contains("not available") || detail.contains("private") ||
                         detail.contains("removed"))
                    message = "This song is unavailable for your account or region.";
                else if (detail.contains("403") || detail.contains("po token") ||
                         detail.contains("challenge"))
                    message =
                        "YouTube rejected the playback request. Please retry or update the playback tools.";
                failed(message);
                return;
            }
            const auto object = QJsonDocument::fromJson(output).object();
            const QUrl url(object["url"].toString());
            if (url.scheme() != "https" || url.port(443) != 443 ||
                !(url.host() == "googlevideo.com" || url.host().endsWith(".googlevideo.com"))) {
                failed("The playback helper did not return a usable audio stream.");
                return;
            }
            const auto headers = object["http_headers"].toObject();
            const auto duration = qint64(object["duration"].toDouble() * 1000);
            work.reset();
            publish(url, headers, duration, 300);
        });
}

AudioResolver::~AudioResolver() {
    cancel();
}
void AudioResolver::cancel() {
    direct.cancel();
    timeout.stop();
    auto oldWork = std::shared_ptr<QTemporaryDir>(std::move(work));
    if (oldWork)
        QFile::remove(oldWork->filePath("cookies.txt"));
    auto oldProcess = std::exchange(process, nullptr);
    if (oldProcess) {
        oldProcess->disconnect(this);
        // Keep the private directory alive until the helper has actually exited.
        if (oldProcess->state() != QProcess::NotRunning) {
            connect(oldProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), oldProcess,
                    [oldProcess, oldWork] { oldProcess->deleteLater(); });
            oldProcess->kill();
        } else
            oldProcess->deleteLater();
    }
    output.clear();
    diagnostic.clear();
    accountCookies.clear();
}
void AudioResolver::failed(QString message) {
    cancel();
    emit error(message);
}
void AudioResolver::resolve(const QString &id, const QByteArray &cookies, bool thorough) {
    const auto videoId = id;
    cancel();
    static const QRegularExpression idPattern("^[A-Za-z0-9_-]{11}$");
    if (!idPattern.match(videoId).hasMatch()) {
        emit error("This item does not have a valid YouTube video ID.");
        return;
    }
    cacheKey = sourceKey(videoId, cookies);
    const auto cached = sources.value(cacheKey);
    if (!thorough && cached.expiry > QDateTime::currentSecsSinceEpoch()) {
        emit cacheHit();
        emit ready(cached.url, cached.headers, cached.duration);
        return;
    }
    this->videoId = videoId;
    accountCookies = cookies;
    if (thorough)
        sources.remove(cacheKey);
    if (useDirect && !thorough) {
        emit stage("native_resolver_started");
        direct.resolve(videoId);
    } else
        launch(!thorough);
}
void AudioResolver::launch(bool fast) {
    if (executable.isEmpty() || runtime.isEmpty()) {
        failed("This song needs the playback helper. Install Velune's playback tools, then restart.");
        return;
    }
    emit stage(fast ? "helper_resolver_started" : "helper_fallback_started");
    fastAttempt = fast;
    work = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/velune-playback-XXXXXX");
    if (!work->isValid()) {
        failed("Could not create a private temporary directory for playback.");
        return;
    }
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/resolver";
    QDir().mkpath(cache);
    QFile::setPermissions(cache, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QStringList args{"--ignore-config",
                     "--no-playlist",
                     "--no-progress",
                     "--no-warnings",
                     "--cache-dir",
                     cache,
                     "--socket-timeout",
                     "12",
                     "--retries",
                     "1",
                     "--skip-download",
                     "--dump-single-json",
                     "--check-formats",
                     "--format",
                     "bestaudio[ext=m4a][protocol=https]/bestaudio[protocol=https]/best[protocol=https]",
                     "--js-runtimes",
                     "deno:" + runtime};
    if (fast) {
        // web_music alone rejects some public videos and favors muxed video.
        // Resolve the public audio clients together, without related metadata or
        // manifests. Keep the small format probe: an extracted URL can still be
        // rejected by the CDN. The full retry retains yt-dlp's default clients.
        args << "--extractor-args"
             << "youtube:player_client=web_music,visionos;player_skip=initial_data;skip=hls,dash";
    }
    if (!fast && !accountCookies.isEmpty()) {
        QFile file(work->filePath("cookies.txt"));
        if (!file.open(QIODevice::WriteOnly) ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
            file.write(accountCookies) != accountCookies.size()) {
            failed("Could not prepare your account session for playback.");
            return;
        }
        file.close();
        args << "--cookies" << file.fileName();
    }
    args << "--" << "https://music.youtube.com/watch?v=" + videoId;
    createProcess();
    process->setProgram(executable);
    process->setArguments(args);
    process->start();
    timeout.start();
}
} // namespace velune
