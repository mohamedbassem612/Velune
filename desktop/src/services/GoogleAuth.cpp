#include "GoogleAuth.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cstring>
#ifdef VELUNE_KEYCHAIN
#include <qt6keychain/keychain.h>
#endif
namespace velune {
namespace {
const auto youtubeScope = QStringLiteral("https://www.googleapis.com/auth/youtube.readonly");
QString randomUrlToken() {
    QByteArray bytes(32, '\0');
    for (int i = 0; i < bytes.size(); i += 4) {
        auto value = QRandomGenerator::system()->generate();
        memcpy(bytes.data() + i, &value, sizeof(value));
    }
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
} // namespace
bool GoogleOAuthConfig::valid() const {
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9_-]+\\.apps\\.googleusercontent\\.com$"));
    return pattern.match(clientId).hasMatch();
}
GoogleOAuthConfig GoogleOAuthConfig::load(const QString &directory) {
    auto path = qEnvironmentVariable("VELUNE_GOOGLE_OAUTH_CONFIG");
    if (path.isEmpty()) {
        path = directory + "/google-oauth.json";
        if (!QFile::exists(path))
            path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/google-oauth.json";
    }
    QFile file(path);
    QJsonObject object;
    if (file.open(QIODevice::ReadOnly) && file.size() <= 65536) {
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        // Google Console's Desktop app download has an "installed" object.
        // A Web application client is deliberately not accepted here.
        if (root.contains("installed"))
            object = root["installed"].toObject();
        else if (!root.contains("web"))
            object = root;
    }
    GoogleOAuthConfig result{object["client_id"].toString(), object["client_secret"].toString()};
    const auto clientId = qEnvironmentVariable("VELUNE_GOOGLE_CLIENT_ID");
    if (!clientId.isEmpty())
        result.clientId = clientId;
    const auto secret = qEnvironmentVariable("VELUNE_GOOGLE_CLIENT_SECRET");
    if (!secret.isEmpty())
        result.clientSecret = secret;
    return result;
}
GoogleAuth::GoogleAuth(GoogleOAuthConfig config, QString storageKey, QObject *parent)
    : GoogleAuth(std::move(config), std::move(storageKey), GoogleOAuthOptions{}, parent) {}
GoogleAuth::GoogleAuth(GoogleOAuthConfig c, QString key, GoogleOAuthOptions opts, QObject *parent)
    : QObject(parent), config(std::move(c)), storageKey(std::move(key)), options(std::move(opts)) {
    if (!options.openBrowser)
        options.openBrowser = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    deadline.setSingleShot(true);
    refreshTimer.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this,
            [this] { fail("Google sign-in timed out. Please try again."); });
    connect(&refreshTimer, &QTimer::timeout, this, &GoogleAuth::refresh);
    connect(&callback, &QTcpServer::newConnection, this, &GoogleAuth::acceptCallback);
}
void GoogleAuth::setBusy(bool value) {
    if (pending == value)
        return;
    pending = value;
    emit busyChanged(value);
}
GoogleAuth::~GoogleAuth() {
    resetFlow();
    refreshTimer.stop();
}
void GoogleAuth::resetFlow() {
    ++generation;
    refreshingSession = false;
    callback.close();
    deadline.stop();
    state.clear();
    verifier.clear();
    redirect.clear();
    const auto oldRequests = requests;
    requests.clear();
    for (auto reply : oldRequests) {
        reply->abort();
        reply->deleteLater();
    }
    const auto oldClients = clients;
    clients.clear();
    for (auto socket : oldClients) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    setBusy(false);
}
void GoogleAuth::cancel() {
    resetFlow();
    emit status("Google sign-in cancelled.");
}
void GoogleAuth::fail(QString message) {
    const bool lostSession = refreshingSession;
    resetFlow();
    if (lostSession && verified) {
        verified = false;
        bearer.clear();
        emit authenticatedChanged(false);
    }
    emit error(message);
}
void GoogleAuth::start() {
    if (pending) {
        emit status("Complete Google sign-in in your browser, or cancel and try again.");
        return;
    }
    if (!config.valid()) {
        emit error("Google sign-in is not configured for this Velune installation. The app owner needs to "
                   "register Velune with Google once.");
        return;
    }
    resetFlow();
    if (!callback.listen(QHostAddress::LocalHost, 0)) {
        fail("Could not start the local Google sign-in callback.");
        return;
    }
    state = randomUrlToken();
    verifier = randomUrlToken();
    redirect = QString("http://127.0.0.1:%1/oauth2/callback").arg(callback.serverPort());
    const auto challenge = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                               .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url(options.authorizationEndpoint);
    QUrlQuery query;
    query.addQueryItem("client_id", config.clientId);
    query.addQueryItem("redirect_uri", redirect);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", "openid email profile " + youtubeScope);
    query.addQueryItem("state", state);
    query.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("access_type", "offline");
    query.addQueryItem("prompt", "select_account consent");
    url.setQuery(query);
    setBusy(true);
    deadline.start(options.timeoutMs);
    if (!options.openBrowser(url)) {
        fail("Could not open your default browser. Check your system browser setting and try again.");
        return;
    }
    emit status(
        "Choose your Google account in your browser. Velune will connect automatically when you return.");
}
void GoogleAuth::acceptCallback() {
    while (callback.hasPendingConnections()) {
        auto socket = callback.nextPendingConnection();
        if (!socket->peerAddress().isLoopback() || clients.size() >= 8) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        clients.insert(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            clients.remove(socket);
            socket->deleteLater();
        });
        auto timeout = new QTimer(socket);
        timeout->setSingleShot(true);
        timeout->start(5000);
        connect(timeout, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            auto header = socket->property("header").toByteArray() + socket->readAll();
            if (header.size() > 8192) {
                respond(socket, 413, "Request rejected", "The callback was too large.");
                return;
            }
            if (!header.contains("\r\n\r\n")) {
                socket->setProperty("header", header);
                return;
            }
            socket->disconnect(this);
            clients.remove(socket);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            consumeCallback(socket, header);
        });
    }
}
void GoogleAuth::respond(QTcpSocket *socket, int code, QString title, QString text) {
    const auto body =
        QString("<!doctype html><html><head><meta charset=\"utf-8\"><title>Velune</title>"
                "<style>body{background:#000;color:#fff;font:18px sans-serif;max-width:640px;margin:15vh "
                "auto;padding:24px}p{color:#a3a3a3}</style>"
                "</head><body><h1>%1</h1><p>%2</p></body></html>")
            .arg(title.toHtmlEscaped(), text.toHtmlEscaped())
            .toUtf8();
    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK\r\n" : " Error\r\n") +
                  "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: "
                  "no-referrer\r\n"
                  "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\nConnection: "
                  "close\r\nContent-Length: " +
                  QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
}
void GoogleAuth::consumeCallback(QTcpSocket *socket, const QByteArray &header) {
    auto lines = header.split('\n');
    const auto first = lines.value(0).trimmed().split(' ');
    if (first.size() != 3 || first[0] != "GET" || !first[1].startsWith('/')) {
        respond(socket, 400, "Request rejected", "Return to Velune and try signing in again.");
        return;
    }
    QUrl url = QUrl::fromEncoded("http://127.0.0.1" + first[1]);
    if (url.path() != "/oauth2/callback") {
        respond(socket, 404, "Not found", "Return to Velune.");
        return;
    }
    QUrlQuery query(url);
    auto states = query.allQueryItemValues("state", QUrl::FullyDecoded);
    QByteArray host;
    for (auto line : lines)
        if (line.toLower().startsWith("host:"))
            host = line.mid(5).trimmed();
    if (!pending || state.isEmpty() || states.size() != 1 || states[0] != state ||
        host != QByteArray("127.0.0.1:") + QByteArray::number(callback.serverPort())) {
        respond(socket, 400, "Request rejected", "This response did not match your sign-in request.");
        return;
    }
    auto codes = query.allQueryItemValues("code", QUrl::FullyDecoded);
    if (query.hasQueryItem("error")) {
        respond(socket, 200, "Sign-in cancelled",
                "Google did not grant access. You can return to Velune and try again.");
        fail("Google sign-in was cancelled or access was denied.");
        return;
    }
    if (codes.size() != 1 || codes[0].isEmpty() || codes[0].size() > 4096) {
        respond(socket, 400, "Request rejected", "Google did not return a valid sign-in code.");
        return;
    }
    respond(socket, 200, "Sign-in received",
            "Velune is verifying your account. You can close this tab and return to the app.");
    callback.close();
    state.clear();
    exchange(codes[0]);
}
void GoogleAuth::exchange(const QString &code) {
    QUrlQuery form;
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code", code);
    form.addQueryItem("redirect_uri", redirect);
    form.addQueryItem("code_verifier", verifier);
    verifier.clear();
    requestToken(form, false);
}
void GoogleAuth::requestToken(QUrlQuery form, bool refreshing) {
    refreshingSession = refreshing;
    form.addQueryItem("client_id", config.clientId);
    if (!config.clientSecret.isEmpty())
        form.addQueryItem("client_secret", config.clientSecret);
    QByteArray encoded;
    for (const auto &item : form.queryItems(QUrl::FullyDecoded)) {
        if (!encoded.isEmpty())
            encoded += '&';
        encoded += QUrl::toPercentEncoding(item.first) + '=' + QUrl::toPercentEncoding(item.second);
    }
    request(options.tokenEndpoint, encoded, {},
            [this, refreshing](QJsonObject tokens) { verifyToken(tokens, refreshing); });
}
void GoogleAuth::request(const QUrl &url, const QByteArray &body, const QByteArray &access,
                         std::function<void(QJsonObject)> done) {
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (!access.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + access);
    QNetworkReply *reply;
    if (body.isEmpty())
        reply = network.get(request);
    else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = network.post(request, body);
    }
    requests.insert(reply);
    const auto currentGeneration = generation;
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 65536)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentGeneration, done] {
        requests.remove(reply);
        reply->deleteLater();
        if (currentGeneration != generation)
            return;
        const auto statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto json = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300 ||
            !json.isObject()) {
            fail("Google could not complete sign-in. Check your connection and Velune's Google registration, "
                 "then try again.");
            return;
        }
        done(json.object());
    });
}
void GoogleAuth::verifyToken(const QJsonObject &tokens, bool refreshing) {
    const auto access = tokens["access_token"].toString();
    const auto tokenScopes = tokens["scope"].toString(refreshing ? scopes : QString());
    const int lifetime = tokens["expires_in"].toInt();
    if (access.isEmpty() || access.contains('\r') || access.contains('\n') ||
        tokens["token_type"].toString().compare("Bearer", Qt::CaseInsensitive) != 0 || lifetime <= 0 ||
        !tokenScopes.split(' ').contains(youtubeScope)) {
        fail("Google did not grant the account and YouTube permissions Velune needs. Please try again and "
             "allow access.");
        return;
    }
    const auto refreshValue = tokens["refresh_token"].toString(refreshing ? refreshToken : QString());
    request(options.userInfoEndpoint, {}, access.toUtf8(),
            [this, access, tokenScopes, refreshValue, lifetime](QJsonObject identity) {
                if (identity["sub"].toString().isEmpty() || identity["email"].toString().isEmpty() ||
                    !identity["email_verified"].toBool()) {
                    fail("Google did not confirm your account identity. Please try signing in again.");
                    return;
                }
                refreshingSession = false;
                bearer = access;
                refreshToken = refreshValue;
                scopes = tokenScopes;
                profile = identity;
                verified = true;
                deadline.stop();
                setBusy(false);
                refreshTimer.start(qMax(1000, qMin(lifetime, 86400) * 1000 - 60000));
                if (refreshToken.isEmpty())
                    clearSession();
                else
                    saveSession();
                emit authenticatedChanged(true);
                emit status("Connected to Google as " + accountEmail() + ".");
            });
}
void GoogleAuth::refresh() {
    if (pending || refreshToken.isEmpty() || !config.valid())
        return;
    resetFlow();
    setBusy(true);
    deadline.start(30000);
    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", refreshToken);
    requestToken(form, true);
}
void GoogleAuth::restore() {
#ifdef VELUNE_KEYCHAIN
    if (!config.valid() || !options.persistSession || pending)
        return;
    const auto currentGeneration = generation;
    auto job = new QKeychain::ReadPasswordJob("Velune", this);
    job->setKey(storageKey);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job, currentGeneration] {
        if (generation != currentGeneration || pending || job->error() != QKeychain::NoError)
            return;
        const auto stored = QJsonDocument::fromJson(job->binaryData()).object();
        refreshToken = stored["refresh_token"].toString();
        scopes = stored["scope"].toString();
        if (!refreshToken.isEmpty())
            refresh();
    });
    job->start();
#endif
}
void GoogleAuth::saveSession() {
#ifdef VELUNE_KEYCHAIN
    if (!options.persistSession || refreshToken.isEmpty())
        return;
    auto job = new QKeychain::WritePasswordJob("Velune", this);
    job->setKey(storageKey);
    job->setInsecureFallback(false);
    job->setBinaryData(QJsonDocument(QJsonObject{{"refresh_token", refreshToken}, {"scope", scopes}})
                           .toJson(QJsonDocument::Compact));
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job *job) {
        if (job->error() != QKeychain::NoError)
            emit status("You are signed in for this session. Your system keyring could not save the login.");
    });
    job->start();
#endif
}
void GoogleAuth::clearSession() {
#ifdef VELUNE_KEYCHAIN
    if (!options.persistSession)
        return;
    auto job = new QKeychain::DeletePasswordJob("Velune", this);
    job->setKey(storageKey);
    job->setInsecureFallback(false);
    job->start();
#endif
}
void GoogleAuth::logout() {
    resetFlow();
    refreshTimer.stop();
    clearSession();
    bearer.clear();
    refreshToken.clear();
    scopes.clear();
    profile = {};
    verified = false;
    emit authenticatedChanged(false);
}
} // namespace velune
