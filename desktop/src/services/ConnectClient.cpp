#include "ConnectClient.h"
#include "ConnectHost.h"
#include "ConnectMediaProvider.h"
#include "ConnectSession.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSysInfo>
#include <QUuid>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
namespace velune {
bool ConnectClient::mediaBusy() const { return !mediaCalls.isEmpty() || (provider && provider->busy()); }
static constexpr int MaxFrame = 12 * 1024 * 1024;
static QString uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QByteArray ConnectClient::fingerprint(const QSslCertificate &cert) {
    auto der = cert.toDer();
    auto ptr = reinterpret_cast<const unsigned char *>(der.constData());
    X509 *x = d2i_X509(nullptr, &ptr, der.size());
    if (!x)
        return {};
    EVP_PKEY *pub = X509_get_pubkey(x);
    unsigned char *out = nullptr;
    int size = pub ? i2d_PUBKEY(pub, &out) : 0;
    QByteArray hash = size > 0 ? QCryptographicHash::hash(QByteArray(reinterpret_cast<char *>(out), size),
                                                          QCryptographicHash::Sha256)
                                     .toHex()
                               : QByteArray();
    OPENSSL_free(out);
    EVP_PKEY_free(pub);
    X509_free(x);
    return hash;
}
bool ConnectClient::createIdentity() {
    auto dir = library->directory() + "/connect";
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile c(dir + "/identity.pem"), k(dir + "/identity-key.pem");
    if (c.exists() && k.exists()) {
        if (!c.open(QIODevice::ReadOnly) || !k.open(QIODevice::ReadOnly))
            return false;
        certificate = QSslCertificate(c.readAll());
        key = QSslKey(k.readAll(), QSsl::Rsa);
    } else {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY *pkey = nullptr;
        if (!ctx)
            return false;
        if (EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return false;
        }
        EVP_PKEY_CTX_free(ctx);
        X509 *x = X509_new();
        X509_set_version(x, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(x), QRandomGenerator::global()->bounded(1, 2147483647));
        X509_gmtime_adj(X509_get_notBefore(x), -3600);
        X509_gmtime_adj(X509_get_notAfter(x), 315360000L);
        X509_set_pubkey(x, pkey);
        X509_NAME *n = X509_get_subject_name(x);
        X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>("Velune Desktop"), -1, -1, 0);
        X509_set_issuer_name(x, n);
        if (!X509_sign(x, pkey, EVP_sha256())) {
            X509_free(x);
            EVP_PKEY_free(pkey);
            return false;
        }
        BIO *cb = BIO_new(BIO_s_mem()), *kb = BIO_new(BIO_s_mem());
        PEM_write_bio_X509(cb, x);
        PEM_write_bio_PrivateKey(kb, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        char *raw = nullptr;
        long len = BIO_get_mem_data(cb, &raw);
        QByteArray cp(raw, len);
        len = BIO_get_mem_data(kb, &raw);
        QByteArray kp(raw, len);
        BIO_free(cb);
        BIO_free(kb);
        X509_free(x);
        EVP_PKEY_free(pkey);
        for (auto item : {qMakePair(dir + "/identity.pem", cp), qMakePair(dir + "/identity-key.pem", kp)}) {
            QSaveFile file(item.first);
            if (!file.open(QIODevice::WriteOnly))
                return false;
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            if (file.write(item.second) != item.second.size() || !file.commit())
                return false;
        }
        certificate = QSslCertificate(cp);
        key = QSslKey(kp, QSsl::Rsa);
    }
    localId = QString::fromLatin1(fingerprint(certificate));
    return !certificate.isNull() && !key.isNull() && !localId.isEmpty();
}
ConnectClient::ConnectClient(Library *l, QObject *parent) : QObject(parent), library(l) {
    provider = new ConnectMediaProvider(l, this);
    createIdentity();
    heartbeat.setInterval(5000);
    watchdog.setInterval(16000);
    watchdog.setSingleShot(true);
    retry.setSingleShot(true);
    connect(&heartbeat, &QTimer::timeout, this, [this] {
        send({{"type", "ping"}});
        publishLibrary(true);
    });
    connect(&watchdog, &QTimer::timeout, this, [this] { fail("Velune Connect timed out."); });
    connect(&retry, &QTimer::timeout, this, &ConnectClient::open);
    connect(l, &Library::changed, this, [this] { publishLibrary(); });
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    tls.setLocalCertificate(certificate);
    tls.setPrivateKey(key);
    hostServer = new ConnectHost(l, localId, tls, this);
    connect(hostServer, &ConnectHost::changed, this, [this](QJsonObject s) {
        if (!hosting())
            return;
        state = s;
        emit stateChanged(state);
        emit statusChanged(rendersLocally() ? "This computer" : "Connected device");
    });
    connect(hostServer, &ConnectHost::availabilityChanged, this, [this] {
        emit serverChanged();
        if (explicitDisconnect) {
            if (hosting())
                state = hostServer->playback();
            emit statusChanged("This computer");
            emit stateChanged(hosting() ? state : QJsonObject{});
        }
    });
    connect(hostServer, &ConnectHost::error, this, &ConnectClient::error);
    connect(hostServer, &ConnectHost::prepareRequested, this, &ConnectClient::prepareRequested);
    connect(hostServer, &ConnectHost::releaseRequested, this, &ConnectClient::releaseRequested);
    connect(hostServer, &ConnectHost::abortRequested, this, &ConnectClient::abortRequested);
    connect(hostServer, &ConnectHost::commandRequested, this, &ConnectClient::commandRequested);
    connect(hostServer, &ConnectHost::mediaReply, this, &ConnectClient::mediaResponse);
    connect(hostServer, &ConnectHost::peerLost, this, &ConnectClient::cancelPeerMedia);
    connect(hostServer, &ConnectHost::peerLost, provider, &ConnectMediaProvider::disconnectPeer);
    connect(hostServer, &ConnectHost::mediaRequest, this, [this](QString peer, QJsonObject m) {
        provider->serve(peer, m, [this, peer](QJsonObject r) { hostServer->sendMedia(peer, r); });
    });
}
bool ConnectClient::hosting() const {
    return explicitDisconnect && hostServer && hostServer->active();
}
bool ConnectClient::connected() const {
    return hosting() || (authorized && sessionReady);
}
bool ConnectClient::hasTarget() const {
    return hosting() || !explicitDisconnect;
}
bool ConnectClient::rendersLocally() const {
    return connected() && state["rendererId"].toString() == localId;
}
quint16 ConnectClient::listeningPort() const {
    return explicitDisconnect && hostServer ? hostServer->port() : 0;
}
bool ConnectClient::localAddress(const QHostAddress &a) {
    if (a.isLoopback() || a.isLinkLocal())
        return true;
    if (a.protocol() == QAbstractSocket::IPv4Protocol) {
        auto n = a.toIPv4Address();
        return (n >> 24) == 10 || (n >> 20) == 0xac1 || (n >> 16) == 0xc0a8;
    }
    auto v = a.toIPv6Address();
    return a.protocol() == QAbstractSocket::IPv6Protocol && (v[0] & 0xfe) == 0xfc;
}
ConnectClient::~ConnectClient() {
    // Closing the app preserves the coordinator checkpoint; an explicit detach clears it.
    explicitDisconnect = true;
    retry.stop();
    heartbeat.stop();
    watchdog.stop();
    if (socket) {
        socket->disconnect(this);
        socket->abort();
    }
    provider->disconnectPeer("upstream");
    hostServer->disconnect(this);
    hostServer->setEnabled(false);
}
void ConnectClient::attach(QString h, quint16 p, QString id) {
    if (!explicitDisconnect && expected == id && host == h && port == p) {
        if (connected())
            send({{"type", "transfer"},
                  {"sessionId", state["sessionId"]},
                  {"epoch", state["epoch"]},
                  {"target", expected}});
        else {
            initialSelection = false;
            retry.stop();
            open();
        }
        return;
    }
    detach();
    if (id.size() != 64 || !localAddress(QHostAddress(h)) || p == 0) {
        emit error("Select a valid local Velune device.");
        return;
    }
    host = h;
    port = p;
    expected = id;
    explicitDisconnect = false;
    initialSelection = true;
    hostServer->setEnabled(false);
    emit serverChanged();
    attempts = 0;
    open();
}
void ConnectClient::detach() {
    if (!preparation.isEmpty()) {
        emit abortRequested(preparation);
        preparation.clear();
    }
    cancelPeerMedia(expected);
    if (hostServer)
        hostServer->disengage();
    explicitDisconnect = true;
    retry.stop();
    heartbeat.stop();
    watchdog.stop();
    authorized = false;
    sessionReady = false;
    state = {};
    if (socket) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        socket = nullptr;
    }
    provider->disconnectPeer("upstream");
    for (auto t : pendingCommands)
        t->deleteLater();
    pendingCommands.clear();
    rendererCommands.clear();
    rendererCommandOrder.clear();
    if (hostServer)
        hostServer->setEnabled(true);
    emit serverChanged();
    emit statusChanged("This computer");
    emit stateChanged({});
}
void ConnectClient::open() {
    if (explicitDisconnect)
        return;
    if (localId.isEmpty()) {
        emit error("Could not load a Connect TLS identity.");
        return;
    }
    if (socket) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    socket = new QSslSocket(this);
    outgoing = incoming = 0;
    buffer.clear();
    authorized = false;
    sessionReady = false;
    rendererSequence = 0;
    publishedLibrary = {};
    libraryExchange.clear();
    socket->setProtocol(QSsl::TlsV1_2OrLater);
    socket->setReadBufferSize(MaxFrame + 65536);
    socket->setLocalCertificate(certificate);
    socket->setPrivateKey(key);
    socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
    connect(socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        const auto cert = socket->peerCertificate();
        auto now = QDateTime::currentDateTimeUtc();
        bool valid = QString::fromLatin1(fingerprint(cert)) == expected && cert.effectiveDate() <= now &&
                     cert.expiryDate() > now && localAddress(socket->peerAddress());
        for (const auto &e : errors)
            if (e.error() != QSslError::SelfSignedCertificate && e.error() != QSslError::HostNameMismatch)
                valid = false;
        if (valid)
            socket->ignoreSslErrors(errors);
        else
            fail("The device TLS identity did not match discovery.");
    });
    connect(socket, &QSslSocket::encrypted, this, [this] {
        if (QString::fromLatin1(fingerprint(socket->peerCertificate())) != expected ||
            !localAddress(socket->peerAddress())) {
            fail("Invalid Connect identity.");
            return;
        }
        send({{"type", "identity"},
              {"id", localId},
              {"name", QSysInfo::machineHostName().left(100)},
              {"mediaVersion", 1},
              {"assetVersion", 1},
              {"protocol", 4}});
    });
    connect(socket, &QSslSocket::readyRead, this, &ConnectClient::read);
    connect(socket, &QSslSocket::disconnected, this, [this] { fail("Device disconnected. Reconnecting…"); });
    connect(socket, &QSslSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { fail(socket->errorString()); });
    emit statusChanged("Connecting…");
    watchdog.start(10000);
    socket->connectToHostEncrypted(host, port);
}
void ConnectClient::fail(const QString &message) {
    if (!socket || retry.isActive() || explicitDisconnect)
        return;
    heartbeat.stop();
    watchdog.stop();
    authorized = false;
    sessionReady = false;
    if (!preparation.isEmpty()) {
        emit abortRequested(preparation);
        preparation.clear();
    }
    cancelPeerMedia(expected);
    retry.start(1000);
    socket->abort();
    provider->disconnectPeer("upstream");
    for (auto t : pendingCommands)
        t->deleteLater();
    pendingCommands.clear();
    emit statusChanged("Device offline");
    emit error(message);
    retry.start(qMin(30000, 1000 * (1 << qMin(attempts++, 5))) + QRandomGenerator::global()->bounded(400));
}
void ConnectClient::send(QJsonObject m) {
    if (!socket || !socket->isEncrypted())
        return;
    m["seq"] = double(outgoing + 1);
    auto bytes = QJsonDocument(m).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFrame) {
        emit error("Connect message is too large.");
        return;
    }
    if (socket->bytesToWrite() > 2 * MaxFrame) {
        fail("Connect output exceeded its buffer limit.");
        return;
    }
    ++outgoing;
    socket->write(bytes + '\n');
}
void ConnectClient::publishLibrary(bool retryPending) {
    if (!authorized || !sessionReady)
        return;
    const auto snapshot = library->snapshot();
    if (snapshot == publishedLibrary && (!retryPending || libraryExchange.isEmpty()))
        return;
    publishedLibrary = snapshot;
    libraryExchange = uuid();
    send({{"type", "library"}, {"exchangeId", libraryExchange}, {"snapshot", snapshot}});
}
bool ConnectClient::decodeFrame(QByteArray &buf, qint64 &seq, QJsonObject &m, QString &err) {
    auto end = buf.indexOf('\n');
    if (end < 0) {
        if (buf.size() > MaxFrame)
            err = "Connect frame exceeds size limit.";
        return false;
    }
    if (end > MaxFrame) {
        err = "Connect frame exceeds size limit.";
        return false;
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(buf.left(end), &parse);
    buf.remove(0, end + 1);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        err = "Invalid Connect JSON.";
        return false;
    }
    m = doc.object();
    if (m["seq"].toDouble() != double(seq + 1)) {
        err = "Invalid Connect sequence.";
        return false;
    }
    ++seq;
    return true;
}
void ConnectClient::read() {
    buffer += socket->readAll();
    while (true) {
        QJsonObject m;
        QString e;
        if (!decodeFrame(buffer, incoming, m, e)) {
            if (!e.isEmpty())
                fail(e);
            break;
        }
        watchdog.start();
        receive(m);
        if (!socket || retry.isActive())
            break;
    }
}
void ConnectClient::receive(QJsonObject m) {
    QString type = m["type"].toString();
    if (!authorized) {
        if (type != "identity" || m["protocol"].toInt() != 4 || m["id"].toString() != expected) {
            fail("Incompatible Connect handshake.");
            return;
        }
        peerMediaVersion = m["mediaVersion"].toInt();
        peerAssetVersion = m["assetVersion"].toInt();
        authorized = true;
        name = m["name"].toString().left(100);
        attempts = 0;
        auto initial = lastLocalState;
        if (!normalizeSession(initial))
            initial = {{"queue", QJsonArray{}}, {"order", QJsonArray{}}, {"index", -1}};
        send({{"type", "connect"},
              {"startPlayback", initialSelection && !initial["queue"].toArray().isEmpty()},
              {"mediaVersion", 1},
              {"assetVersion", 1},
              {"state", initial}});
        initialSelection = false;
        heartbeat.start();
        return;
    }
    if (type == "state") {
        if (m["hostId"].toString() != expected) {
            fail("Invalid coordinator.");
            return;
        }
        auto s = m["state"].toObject();
        if (s["sessionId"].toString().isEmpty())
            return;
        if (state["sessionId"] == s["sessionId"] && !state.isEmpty() && sessionReady &&
            (s["revision"].toDouble() <= state["revision"].toDouble() ||
             s["epoch"].toDouble() < state["epoch"].toDouble()))
            return;
        if (s.contains("queue") && s["queue"].toArray().size() > 20000) {
            fail("Queue exceeds limit.");
            return;
        }
        if (!s.contains("queue") && state["sessionId"] != s["sessionId"]) {
            fail("Missing queue on new session.");
            return;
        }
        if (!s.contains("queue"))
            s["queue"] = state["queue"];
        if (!s.contains("order"))
            s["order"] = state["order"];
        if (!normalizeSession(s, state)) {
            fail("Invalid Connect session.");
            return;
        }
        if (!preparation.isEmpty() && s["rendererId"].toString() == localId &&
            (s["sessionId"] != state["sessionId"] || s["epoch"].toDouble() > state["epoch"].toDouble()))
            preparation.clear();
        state = s;
        sessionReady = true;
        emit stateChanged(state);
        emit statusChanged(name);
        publishLibrary();
    } else if (type == "ping")
        send({{"type", "pong"}});
    else if (type == "ack") {
        auto id = m["commandId"].toString();
        auto t = pendingCommands.take(id);
        if (t)
            t->deleteLater();
        if (!m["ok"].toBool())
            emit error("The device rejected that command.");
    } else if (type == "library") {
        if (library->merge(m["snapshot"].toObject()))
            send({{"type", "libraryAck"}, {"exchangeId", m["exchangeId"]}});
        else
            emit error("Unsupported library snapshot.");
    } else if (type == "libraryAck") {
        if (m["exchangeId"].toString() == libraryExchange)
            libraryExchange.clear();
    } else if (type == "mediaReply")
        mediaResponse(expected, m);
    else if (type == "mediaRequest")
        media(m);
    else if (type == "mediaCancel")
        provider->serve("upstream", m, [this](QJsonObject r) { send(r); });
    else if (type == "prepare" && sessionEnvelope(m, state)) {
        auto snapshot = m["state"].toObject();
        const auto id = m["transferId"].toString();
        if (preparation.isEmpty() && !id.isEmpty() && id.size() <= 128 && normalizeSession(snapshot)) {
            preparation = id;
            emit prepareRequested(id, snapshot);
        }
    } else if (type == "release" && sessionEnvelope(m, state) && rendersLocally())
        emit releaseRequested(m["transferId"].toString());
    else if (type == "abort" && sessionEnvelope(m, state) && m["transferId"].toString() == preparation) {
        emit abortRequested(preparation);
        preparation.clear();
    } else if (type == "command") {
        const auto id = m["commandId"].toString();
        const bool valid = sessionEnvelope(m, state) && rendersLocally() && preparation.isEmpty() &&
                           !id.isEmpty() && id.size() <= 128;
        if (valid && !rendererCommands.contains(id)) {
            rendererCommands.insert(id);
            rendererCommandOrder.append(id);
            if (rendererCommandOrder.size() > 512)
                rendererCommands.remove(rendererCommandOrder.takeFirst());
            emit commandRequested(m["command"].toObject());
        }
        send({{"type", "ack"}, {"commandId", id}, {"ok", valid}});
    } else if (type == "transferFailed" && sessionEnvelope(m, state)) {
        if (m["pauseSource"].toBool() && rendersLocally())
            emit commandRequested({{"name", "pause"}});
        emit error("Playback transfer failed. Reconnect and resume if playback is paused.");
    }
}
void ConnectClient::command(QJsonObject c) {
    if (!connected()) {
        emit error("Connect a device first.");
        return;
    }
    if (hosting()) {
        hostServer->command(c);
        return;
    }
    auto id = uuid();
    send({{"type", "command"},
          {"sessionId", state["sessionId"]},
          {"epoch", state["epoch"]},
          {"commandId", id},
          {"command", c}});
    auto t = new QTimer(this);
    t->setSingleShot(true);
    pendingCommands[id] = t;
    connect(t, &QTimer::timeout, this, [this, id] {
        auto timer = pendingCommands.take(id);
        if (timer)
            timer->deleteLater();
        emit error("The device did not acknowledge the command.");
    });
    t->start(8000);
}
void ConnectClient::play(const QList<Track> &tracks, int index) {
    QJsonArray a;
    for (const auto &t : tracks)
        a.append(t.wire());
    command({{"name", "replace"},
             {"queue", a},
             {"index", index},
             {"position", 0},
             {"playing", true},
             {"queueTitle", "Velune Desktop"}});
}
void ConnectClient::chooseThisComputer() {
    if (!connected()) {
        detach();
        hostServer->setEnabled(true);
        emit serverChanged();
        return;
    }
    if (rendersLocally())
        return;
    if (hosting())
        hostServer->transfer(localId);
    else
        send({{"type", "transfer"},
              {"sessionId", state["sessionId"]},
              {"epoch", state["epoch"]},
              {"target", localId}});
}
void ConnectClient::rememberLocalPlayback(QJsonObject snapshot) {
    lastLocalState = snapshot;
    if (explicitDisconnect && !hosting())
        hostServer->localState(snapshot);
}
void ConnectClient::publishRendererState(QJsonObject snapshot) {
    if (hosting())
        hostServer->localState(snapshot);
    else if (rendersLocally() && preparation.isEmpty()) {
        snapshot["sessionId"] = state["sessionId"];
        snapshot["epoch"] = state["epoch"];
        snapshot["rendererId"] = localId;
        send({{"type", "rendererState"},
              {"rendererSequence", double(++rendererSequence)},
              {"state", snapshot}});
    }
}
void ConnectClient::prepared(QString transaction, bool ok) {
    if (hosting()) {
        hostServer->prepared(transaction, ok);
        return;
    }
    if (transaction != preparation)
        return;
    send({{"type", ok ? "ready" : "failed"},
          {"sessionId", state["sessionId"]},
          {"epoch", state["epoch"]},
          {"transferId", transaction}});
    if (!ok)
        preparation.clear();
}
void ConnectClient::released(QString transaction, QJsonObject snapshot) {
    if (hosting()) {
        hostServer->released(transaction, snapshot);
        return;
    }
    send({{"type", "released"},
          {"sessionId", state["sessionId"]},
          {"epoch", state["epoch"]},
          {"transferId", transaction},
          {"state", snapshot}});
}
QJsonArray ConnectClient::peerInfo() const {
    if (hosting())
        return hostServer->peerInfo();
    if (connected())
        return {QJsonObject{{"id", expected}, {"name", name}, {"renderer", state["rendererId"] == expected}}};
    return {};
}
QStringList ConnectClient::mediaProviders(bool assets) const {
    if (hosting())
        return hostServer->providers(assets);
    return connected() && (assets ? peerAssetVersion : peerMediaVersion) == 1 ? QStringList{expected}
                                                                              : QStringList{};
}
QString ConnectClient::requestMedia(QString provider, QJsonObject request,
                                    std::function<void(QJsonObject)> done) {
    const auto id = uuid();
    auto timer = new QTimer(this);
    timer->setSingleShot(true);
    mediaCalls[id] = {provider, timer, std::move(done)};
    connect(timer, &QTimer::timeout, this, [this, id] {
        if (!mediaCalls.contains(id))
            return;
        const auto call = mediaCalls.take(id);
        call.timer->deleteLater();
        if (hosting())
            hostServer->sendMedia(call.provider, {{"type", "mediaCancel"}, {"requestId", id}});
        else if (connected() && call.provider == expected)
            send({{"type", "mediaCancel"}, {"requestId", id}});
        call.done({{"ok", false}, {"error", "Media request timed out."}});
    });
    timer->start(request["operation"].toString() == "open" || request["operation"].toString() == "read"
                     ? 120000
                     : 30000);
    request["type"] = "mediaRequest";
    request["requestId"] = id;
    if (hosting() &&
        (hostServer->providers().contains(provider) || hostServer->providers(true).contains(provider)))
        hostServer->sendMedia(provider, request);
    else if (connected() && provider == expected)
        send(request);
    else
        QTimer::singleShot(
            0, this, [this, id, provider] { mediaResponse(provider, {{"requestId", id}, {"ok", false}}); });
    return id;
}
void ConnectClient::cancelMedia(QString id) {
    if (!mediaCalls.contains(id))
        return;
    const auto call = mediaCalls.take(id);
    call.timer->stop();
    call.timer->deleteLater();
    if (hosting())
        hostServer->sendMedia(call.provider, {{"type", "mediaCancel"}, {"requestId", id}});
    else if (connected() && call.provider == expected)
        send({{"type", "mediaCancel"}, {"requestId", id}});
}
void ConnectClient::mediaResponse(QString provider, QJsonObject reply) {
    const auto id = reply["requestId"].toString();
    if (!mediaCalls.contains(id) || mediaCalls[id].provider != provider)
        return;
    const auto call = mediaCalls.take(id);
    call.timer->stop();
    call.timer->deleteLater();
    call.done(reply);
}
void ConnectClient::cancelPeerMedia(QString provider) {
    const auto ids = mediaCalls.keys();
    for (const auto &id : ids)
        if (mediaCalls.contains(id) && mediaCalls[id].provider == provider)
            mediaResponse(provider,
                          {{"requestId", id}, {"ok", false}, {"error", "Media device disconnected."}});
}
void ConnectClient::setProviderCookies(std::function<QByteArray()> fn) {
    provider->setCookies(std::move(fn));
}
void ConnectClient::media(const QJsonObject &m) {
    provider->serve("upstream", m, [this](QJsonObject reply) { send(reply); });
}
} // namespace velune
