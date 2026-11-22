#include "ConnectClient.h"
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
    detach();
}
void ConnectClient::attach(QString h, quint16 p, QString id) {
    detach();
    if (id.size() != 64 || !localAddress(QHostAddress(h)) || p == 0) {
        emit error("Select a valid local Velune device.");
        return;
    }
    host = h;
    port = p;
    expected = id;
    explicitDisconnect = false;
    attempts = 0;
    open();
}
void ConnectClient::detach() {
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
    qDeleteAll(readers);
    readers.clear();
    openedByRequest.clear();
    for (auto t : pendingCommands)
        t->deleteLater();
    pendingCommands.clear();
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
    publishedLibrary = {};
    libraryExchange.clear();
    socket->setProtocol(QSsl::TlsV1_2OrLater);
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
    retry.start(1000);
    socket->abort();
    qDeleteAll(readers);
    readers.clear();
    openedByRequest.clear();
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
    m["seq"] = double(++outgoing);
    auto bytes = QJsonDocument(m).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFrame) {
        emit error("Connect message is too large.");
        return;
    }
    socket->write(bytes + '\n');
}
void ConnectClient::publishLibrary(bool retryPending) {
    if (!connected())
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
        authorized = true;
        name = m["name"].toString().left(100);
        attempts = 0;
        send({{"type", "connect"},
              {"startPlayback", false},
              // Keep TV's independent audio route until desktop can fill complete catalog caches.
              {"state", QJsonObject{{"queue", QJsonArray{}},
                                    {"order", QJsonArray{}},
                                    {"index", -1},
                                    {"position", 0},
                                    {"playing", false}}}});
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
            s["revision"].toDouble() <= state["revision"].toDouble())
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
    } else if (type == "mediaRequest")
        media(m);
    else if (type == "mediaCancel") {
        auto id = m["requestId"].toString();
        delete readers.take(openedByRequest.take(id));
    } else if (type == "prepare")
        send({{"type", "failed"},
              {"sessionId", state["sessionId"]},
              {"epoch", state["epoch"]},
              {"transferId", m["transferId"]}});
    else if (type == "transferFailed")
        emit error("Playback transfer failed. The current output is retained.");
}
void ConnectClient::command(QJsonObject c) {
    if (!connected()) {
        emit error("Connect a device first.");
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
void ConnectClient::media(const QJsonObject &m) {
    auto id = m["requestId"].toString();
    QJsonObject r{{"type", "mediaReply"}, {"requestId", id}, {"ok", false}};
    auto op = m["operation"].toString();
    if (op == "open" && readers.size() < 4) {
        auto song = m["song"].toString();
        auto pos = m["position"].toDouble(-1);
        auto length = m["length"].toDouble(-1);
        auto path = library->mediaPath(song);
        if (!path.isEmpty() && !song.isEmpty() && song.size() <= 256 && pos >= 0 &&
            (length == -1 || length > 0)) {
            auto f = new QFile(path);
            if (f->open(QIODevice::ReadOnly) && pos <= f->size() && f->seek(qint64(pos))) {
                auto handle = uuid();
                f->setProperty("remaining", length < 0 ? f->size() - qint64(pos)
                                                       : qMin(qint64(length), f->size() - qint64(pos)));
                readers[handle] = f;
                openedByRequest[id] = handle;
                r["ok"] = true;
                r["handle"] = handle;
                r["length"] = double(f->property("remaining").toLongLong());
            } else
                delete f;
        }
    } else if (op == "read") {
        auto f = readers.value(m["handle"].toString());
        if (f) {
            auto remaining = f->property("remaining").toLongLong();
            auto bytes = f->read(qMin<qint64>(65536, remaining));
            if (f->error() == QFile::NoError) {
                r["ok"] = true;
                r["data"] = QString::fromLatin1(bytes.toBase64());
                r["eof"] = bytes.isEmpty();
                f->setProperty("remaining", remaining - bytes.size());
            }
        }
    } else if (op == "close") {
        auto handle = m["handle"].toString();
        delete readers.take(handle);
        for (auto it = openedByRequest.begin(); it != openedByRequest.end();) {
            if (it.value() == handle)
                it = openedByRequest.erase(it);
            else
                ++it;
        }
        r["ok"] = true;
    } else
        r["error"] = "No complete local audio is available for this request.";
    send(r);
}
} // namespace velune
