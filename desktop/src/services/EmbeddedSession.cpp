#include "EmbeddedSession.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#ifdef VELUNE_KEYCHAIN
#include <qt6keychain/keychain.h>
#endif
namespace velune {
static const QUrl musicOrigin("https://music.youtube.com/");
static bool musicCookie(const QNetworkCookie &cookie, const QDateTime &now) {
    auto domain = cookie.domain().toLower();
    if (domain.startsWith('.'))
        domain.remove(0, 1);
    const bool domainMatches =
        domain == "music.youtube.com" || (cookie.domain().startsWith('.') && domain == "youtube.com");
    const auto path = cookie.path();
    return domainMatches && (path.isEmpty() || path == "/") &&
           (cookie.isSessionCookie() || cookie.expirationDate() > now) && !cookie.name().isEmpty() &&
           !cookie.name().contains(';') && !cookie.name().contains('=') && !cookie.name().contains('\r') &&
           !cookie.name().contains('\n') && !cookie.value().contains(';') && !cookie.value().contains('\r') &&
           !cookie.value().contains('\n');
}
QByteArray musicCookieHeader(const QList<QNetworkCookie> &cookies, const QDateTime &now) {
    QByteArray result;
    for (const auto &cookie : cookies) {
        if (!musicCookie(cookie, now))
            continue;
        if (!result.isEmpty())
            result += "; ";
        result += cookie.name() + '=' + cookie.value();
    }
    return result;
}
QByteArray musicAuthorization(const QList<QNetworkCookie> &cookies, qint64 seconds) {
    for (const auto &cookie : cookies)
        if (cookie.name() == "SAPISID" && !cookie.value().isEmpty() &&
            musicCookie(cookie, QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC))) {
            const auto timestamp = QByteArray::number(seconds);
            const auto hash = QCryptographicHash::hash(
                timestamp + ' ' + cookie.value() + " https://music.youtube.com", QCryptographicHash::Sha1);
            return "SAPISIDHASH " + timestamp + '_' + hash.toHex();
        }
    return {};
}
static QJsonObject accountHeader(QJsonValue value) {
    if (value.isObject()) {
        const auto object = value.toObject();
        if (object.contains("activeAccountHeaderRenderer"))
            return object["activeAccountHeaderRenderer"].toObject();
        for (auto child : object) {
            auto found = accountHeader(child);
            if (!found.isEmpty())
                return found;
        }
    } else if (value.isArray()) {
        for (auto child : value.toArray()) {
            auto found = accountHeader(child);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}
static QString text(QJsonValue value) {
    const auto object = value.toObject();
    if (object.contains("simpleText"))
        return object["simpleText"].toString();
    QString result;
    for (auto run : object["runs"].toArray())
        result += run.toObject()["text"].toString();
    return result;
}
EmbeddedSession::EmbeddedSession(QString dir, QObject *parent, QNetworkAccessManager *transport, bool persist)
    : QObject(parent), directory(dir), verificationNetwork(transport ? transport : &network),
      persistSession(persist) {
    storageKey =
        "embedded-youtube/" +
        QString::fromLatin1(QCryptographicHash::hash(directory.toUtf8(), QCryptographicHash::Sha256).toHex());
    retry.setInterval(3000);
    connect(&retry, &QTimer::timeout, this, &EmbeddedSession::verify);
    saveTimer.setSingleShot(true);
    saveTimer.setInterval(1500);
    connect(&saveTimer, &QTimer::timeout, this, &EmbeddedSession::save);
}
EmbeddedSession::~EmbeddedSession() {
    closeDialog();
}
void EmbeddedSession::setBusy(bool value) {
    if (pending != value) {
        pending = value;
        emit busyChanged(value);
    }
}
void EmbeddedSession::closeDialog() {
    auto child = helper.data();
    helper.clear();
    if (!child)
        return;
    child->disconnect(this);
    if (child->state() != QProcess::NotRunning) {
        child->write("quit\n");
        child->closeWriteChannel();
        if (!child->waitForFinished(2500)) {
            child->kill();
            child->waitForFinished(1000);
        }
    }
    child->deleteLater();
    helperOutput.clear();
}
void EmbeddedSession::start() {
    if (pending)
        return;
    ++generation;
    cookies.clear();
    ++cookieRevision;
    auto path = QCoreApplication::applicationDirPath() + "/velune-login";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    if (!QFileInfo(path).isExecutable()) {
        emit error("The sign-in helper is missing.");
        return;
    }
    helper = new QProcess(this);
    auto child = helper.data();
    helperOutput.clear();
    connect(child, &QProcess::readyReadStandardOutput, this, [this, child] {
        if (helper != child)
            return;
        helperOutput += child->readAllStandardOutput();
        if (helperOutput.size() > 1024 * 1024) {
            cancel();
            emit error("Invalid sign-in response.");
            return;
        }
        int end;
        while ((end = helperOutput.indexOf('\n')) >= 0) {
            auto line = helperOutput.left(end);
            helperOutput.remove(0, end + 1);
            const auto object = QJsonDocument::fromJson(line).object();
            if (object["type"] != "cookies")
                continue;
            QList<QNetworkCookie> next;
            for (auto value : object["cookies"].toArray())
                for (auto cookie :
                     QNetworkCookie::parseCookies(QByteArray::fromBase64(value.toString().toLatin1())))
                    if (musicCookie(cookie, QDateTime::currentDateTimeUtc()))
                        next.append(cookie);
            cookies = next;
            ++cookieRevision;
            verify();
        }
    });
    connect(child, &QProcess::readyReadStandardError, child, [child] { child->readAllStandardError(); });
    connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError) {
        if (helper == child) {
            cancel();
            emit error("The sign-in window could not start.");
        }
    });
    connect(child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, child](int, QProcess::ExitStatus) {
                if (helper == child)
                    cancel();
            });
    setBusy(true);
    retry.start();
    child->start(path, {});
    emit status("Complete Google sign-in in the Velune sign-in window.");
}
void EmbeddedSession::cancel() {
    ++generation;
    checking = false;
    retry.stop();
    closeDialog();
    setBusy(false);
    emit status("Sign-in cancelled.");
}
void EmbeddedSession::authorize(QNetworkRequest &request) const {
    // Never send account cookies to artwork, media CDNs, Google or a redirect destination.
    const auto url = request.url();
    if (url.scheme() != "https" || url.host() != "music.youtube.com" || url.port(443) != 443 ||
        !url.path().startsWith("/youtubei/v1/"))
        return;
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Cookie", musicCookieHeader(cookies, QDateTime::currentDateTimeUtc()));
    request.setRawHeader("Authorization", musicAuthorization(cookies, QDateTime::currentSecsSinceEpoch()));
    request.setRawHeader("Origin", "https://music.youtube.com");
    request.setRawHeader("X-Origin", "https://music.youtube.com");
    request.setRawHeader("X-Goog-AuthUser", "0");
}
QByteArray EmbeddedSession::playbackCookies() const {
    if (!verified)
        return {};
    QByteArray result("# Netscape HTTP Cookie File\n");
    for (const auto &cookie : cookies) {
        if (!musicCookie(cookie, QDateTime::currentDateTimeUtc()) || cookie.name().contains('\t') ||
            cookie.value().contains('\t'))
            continue;
        result +=
            cookie.domain().toUtf8() + '\t' + (cookie.domain().startsWith('.') ? "TRUE" : "FALSE") + "\t/\t" +
            (cookie.isSecure() ? "TRUE" : "FALSE") + '\t' +
            QByteArray::number(cookie.isSessionCookie() ? 0 : cookie.expirationDate().toSecsSinceEpoch()) +
            '\t' + cookie.name() + '\t' + cookie.value() + '\n';
    }
    return result;
}
void EmbeddedSession::verify() {
    if (checking || (!pending && !verified) ||
        musicAuthorization(cookies, QDateTime::currentSecsSinceEpoch()).isEmpty())
        return;
    checking = true;
    const auto attempt = generation, revision = cookieRevision;
    QNetworkRequest request(
        QUrl("https://music.youtube.com/youtubei/v1/account/account_menu?prettyPrint=false"));
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Referer", "https://music.youtube.com/");
    request.setRawHeader("X-YouTube-Client-Name", "67");
    request.setRawHeader("X-YouTube-Client-Version", "1.20260114.01.00");
    authorize(request);
    auto reply = verificationNetwork->post(
        request,
        R"({"context":{"client":{"clientName":"WEB_REMIX","clientVersion":"1.20260114.01.00","hl":"en"}}})");
    connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > 2 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, revision] {
        reply->deleteLater();
        if (attempt != generation)
            return;
        checking = false;
        if (revision != cookieRevision)
            return;
        const auto header = accountHeader(QJsonDocument::fromJson(reply->readAll()).object());
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
            text(header["accountName"]).isEmpty()) {
            if (!helper) {
                retry.stop();
                setBusy(false);
                emit error("Your saved YouTube Music session could not be verified. Please sign in again.");
            }
            return;
        }
        name = text(header["accountName"]);
        email = text(header["email"]);
        const bool newlyConnected = !verified;
        verified = true;
        retry.stop();
        closeDialog();
        setBusy(false);
        save();
        if (newlyConnected)
            emit authenticatedChanged(true);
        emit status("Connected to YouTube Music as " + name + ".");
    });
}
void EmbeddedSession::restore() {
    if (!persistSession)
        return;
#ifdef VELUNE_KEYCHAIN
    const auto attempt = generation;
    auto job = new QKeychain::ReadPasswordJob("Velune", this);
    job->setKey(storageKey);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job, attempt] {
        if (attempt != generation || pending || job->error() != QKeychain::NoError)
            return;
        const auto array = QJsonDocument::fromJson(job->binaryData()).array();
        for (auto value : array) {
            for (auto cookie :
                 QNetworkCookie::parseCookies(QByteArray::fromBase64(value.toString().toLatin1())))
                if (musicCookie(cookie, QDateTime::currentDateTimeUtc()))
                    cookies.append(cookie);
        }
        if (musicAuthorization(cookies, QDateTime::currentSecsSinceEpoch()).isEmpty())
            return;
        ++cookieRevision;
        setBusy(true);
        verify();
    });
    job->start();
#endif
}
void EmbeddedSession::save() {
#ifdef VELUNE_KEYCHAIN
    if (!verified || !persistSession)
        return;
    QJsonArray array;
    for (const auto &cookie : cookies)
        if (musicCookie(cookie, QDateTime::currentDateTimeUtc()))
            array.append(QString::fromLatin1(cookie.toRawForm().toBase64()));
    auto job = new QKeychain::WritePasswordJob("Velune", this);
    job->setKey(storageKey);
    job->setInsecureFallback(false);
    job->setBinaryData(QJsonDocument(array).toJson(QJsonDocument::Compact));
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job *job) {
        if (job->error() != QKeychain::NoError)
            emit status("Signed in for this session. Your system keyring could not save the account.");
    });
    job->start();
#endif
}
void EmbeddedSession::logout() {
    cancel();
    saveTimer.stop();
    closeDialog();
    cookies.clear();
    ++cookieRevision;
    name.clear();
    email.clear();
    verified = false;
#ifdef VELUNE_KEYCHAIN
    if (persistSession) {
        auto job = new QKeychain::DeletePasswordJob("Velune", this);
        job->setKey(storageKey);
        job->setInsecureFallback(false);
        job->start();
    }
#endif
    emit authenticatedChanged(false);
    emit status("Signed out of Velune.");
}
} // namespace velune
