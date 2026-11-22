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
#include <utility>
namespace velune {
struct CachedSource {
    QUrl url;
    QJsonObject headers;
    qint64 duration = 0, expiry = 0;
};
static QHash<QString, CachedSource> sources;

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
AudioResolver::AudioResolver(QObject *parent, QString binary, QString js)
    : QObject(parent), executable(binary.isEmpty() ? toolPath("yt-dlp") : binary),
      runtime(js.isEmpty() ? toolPath("deno") : js) {
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
                    // The public low-latency client is not available for every song.
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
                else if (detail.contains("max-filesize") || detail.contains("larger than max"))
                    message = "This song exceeds the temporary playback size limit (128 MB).";
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
            if (sources.size() >= 64)
                sources.clear();
            sources[cacheKey] = {url, headers, duration, QDateTime::currentSecsSinceEpoch() + 300};
            work.reset();
            emit ready(url, headers, duration);
        });
}

AudioResolver::~AudioResolver() {
    cancel();
}
void AudioResolver::cancel() {
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
    if (executable.isEmpty() || runtime.isEmpty()) {
        emit error("YouTube playback tools are missing. Install Velune's playback tools, then restart.");
        return;
    }
    cacheKey = videoId + ":" +
               QString::fromLatin1(QCryptographicHash::hash(cookies, QCryptographicHash::Sha256).toHex());
    const auto cached = sources.value(cacheKey);
    if (!thorough && cached.expiry > QDateTime::currentSecsSinceEpoch()) {
        emit cacheHit();
        emit ready(cached.url, cached.headers, cached.duration);
        return;
    }
    this->videoId = videoId;
    accountCookies = cookies;
    launch(!thorough);
}
void AudioResolver::launch(bool fast) {
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
                     "bestaudio[ext=m4a]/bestaudio/best",
                     "--js-runtimes",
                     "deno:" + runtime};
    if (fast) {
        args.removeOne("--check-formats");
        args << "--extractor-args" << "youtube:player_client=web_music";
        args[args.indexOf("--format") + 1] = "18/bestaudio[ext=m4a]/bestaudio/best";
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
