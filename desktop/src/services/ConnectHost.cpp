#include "ConnectHost.h"
#include "ConnectClient.h"
#include "ConnectSession.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>
#include <QUuid>
namespace velune {
namespace {
QString token() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
constexpr qint64 MaxBytes = 12 * 1024 * 1024;
} // namespace
class ConnectPeer : public QObject {
  public:
    explicit ConnectPeer(QObject *parent) : QObject(parent), socket(this), watchdog(this) {}
    QSslSocket socket;
    QTimer watchdog;
    QByteArray buffer;
    QString id, name, exchange;
    qint64 incoming = 0, outgoing = 0, rendererSequence = -1, queueRevision = -1;
    QString session;
    QSet<QString> commands;
    QStringList commandOrder;
    bool identified = false, attached = false;
    int mediaVersion = 0, assetVersion = 0;
};
ConnectHost::ConnectHost(Library *l, QString identity, QSslConfiguration config, QObject *parent)
    : QObject(parent), library(l), id(identity), tls(config), listener(this) {
    QFile saved(l->directory() + "/connect/session.json");
    if (saved.open(QIODevice::ReadOnly)) {
        auto s = QJsonDocument::fromJson(saved.read(12 * 1024 * 1024)).object();
        if (s["coordinatorId"].toString() == id && normalizeSession(s) &&
            !s["sessionId"].toString().isEmpty()) {
            s.remove("coordinatorId");
            s["playing"] = false;
            s["epoch"] = s["epoch"].toDouble() + 1;
            s["revision"] = s["revision"].toDouble() + 1;
            state = s;
        }
    }
    listener.accepted = [this](qintptr fd) { accept(fd); };
    transferTimer.setSingleShot(true);
    connect(&transferTimer, &QTimer::timeout, this, &ConnectHost::failTransfer);
    tick.setInterval(5000);
    connect(&tick, &QTimer::timeout, this, [this] {
        for (auto p : peers)
            if (p->attached) {
                send(p, {{"type", "ping"}});
                publishLibrary(p);
            }
        if (active())
            persist();
    });
    connect(l, &Library::changed, this, [this] {
        publishedLibrary = {};
        for (auto p : peers)
            if (p->attached)
                publishLibrary(p);
    });
    setEnabled(true);
}
ConnectHost::~ConnectHost() {
    setEnabled(false);
}
bool ConnectHost::active() const {
    return engaged;
}
void ConnectHost::disengage() {
    setEnabled(false);
    engaged = false;
    state = {};
    QFile::remove(library->directory() + "/connect/session.json");
    emit availabilityChanged();
}
void ConnectHost::setEnabled(bool value) {
    if (enabled == value)
        return;
    enabled = value;
    if (!value) {
        tick.stop();
        listener.close();
        if (!transferId.isEmpty())
            failTransfer();
        const auto copy = peers;
        for (auto p : copy)
            lost(p);
    } else if (!id.isEmpty() && listener.listen(QHostAddress::AnyIPv4, 0))
        tick.start();
    else
        emit error("Could not start Velune Connect on this computer.");
    emit availabilityChanged();
}
ConnectPeer *ConnectHost::peer(QString identity) const {
    for (auto p : peers)
        if (p->attached && p->id == identity)
            return p;
    return nullptr;
}
void ConnectHost::accept(qintptr descriptor) {
    auto p = new ConnectPeer(this);
    if (!p->socket.setSocketDescriptor(descriptor)) {
        delete p;
        return;
    }
    if (!enabled || peers.size() >= 8 || !ConnectClient::localAddress(p->socket.peerAddress())) {
        delete p;
        return;
    }
    peers.append(p);
    p->socket.setReadBufferSize(MaxBytes + 65536);
    p->socket.setSslConfiguration(tls);
    p->socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    p->watchdog.setSingleShot(true);
    connect(&p->watchdog, &QTimer::timeout, this, [this, p] { lost(p); });
    p->watchdog.start(10000);
    connect(&p->socket, &QSslSocket::sslErrors, this, [this, p](const QList<QSslError> &errors) {
        const auto cert = p->socket.peerCertificate();
        const auto now = QDateTime::currentDateTimeUtc();
        bool valid = !cert.isNull() && cert.effectiveDate() <= now && cert.expiryDate() > now &&
                     ConnectClient::localAddress(p->socket.peerAddress());
        for (const auto &e : errors)
            if (e.error() != QSslError::SelfSignedCertificate)
                valid = false;
        if (valid)
            p->socket.ignoreSslErrors(errors);
        else
            lost(p);
    });
    connect(&p->socket, &QSslSocket::encrypted, this, [this, p] {
        const auto cert = p->socket.peerCertificate();
        const auto now = QDateTime::currentDateTimeUtc();
        p->id = QString::fromLatin1(ConnectClient::fingerprint(cert));
        if (p->id.size() != 64 || cert.effectiveDate() > now || cert.expiryDate() <= now || p->id == id) {
            lost(p);
            return;
        }
        send(p, {{"type", "identity"},
                 {"id", id},
                 {"protocol", 4},
                 {"mediaVersion", 1},
                 {"assetVersion", 1},
                 {"name", QSysInfo::machineHostName().left(100)}});
    });
    connect(&p->socket, &QSslSocket::readyRead, this, [this, p] {
        p->buffer += p->socket.readAll();
        while (peers.contains(p)) {
            QJsonObject m;
            QString error;
            if (!ConnectClient::decodeFrame(p->buffer, p->incoming, m, error)) {
                if (!error.isEmpty())
                    lost(p);
                return;
            }
            p->watchdog.start(16000);
            receive(p, m);
        }
    });
    connect(&p->socket, &QSslSocket::disconnected, this, [this, p] { lost(p); });
    connect(&p->socket, &QSslSocket::errorOccurred, this,
            [this, p](QAbstractSocket::SocketError) { lost(p); });
    p->socket.startServerEncryption();
}
void ConnectHost::send(ConnectPeer *p, QJsonObject m) {
    if (!peers.contains(p) || !p->socket.isEncrypted())
        return;
    m["seq"] = double(p->outgoing + 1);
    const auto bytes = QJsonDocument(m).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxBytes || p->socket.bytesToWrite() > 2 * MaxBytes) {
        const QPointer<ConnectPeer> pending(p);
        QTimer::singleShot(0, this, [this, pending] {
            if (pending)
                lost(pending);
        });
        return;
    }
    ++p->outgoing;
    p->socket.write(bytes + '\n');
}
QJsonObject ConnectHost::envelope(QString type) const {
    return {{"type", type}, {"sessionId", state["sessionId"]}, {"epoch", state["epoch"]}};
}
void ConnectHost::receive(ConnectPeer *p, QJsonObject m) {
    const auto type = m["type"].toString();
    if (!p->identified) {
        if (type != "identity" || m["protocol"].toInt() != 4 || m["id"].toString() != p->id) {
            lost(p);
            return;
        }
        p->identified = true;
        p->name = m["name"].toString().left(100);
        return;
    }
    if (!p->attached) {
        auto initial = m["state"].toObject();
        if (type != "connect" || !normalizeSession(initial)) {
            lost(p);
            return;
        }
        if (auto old = peer(p->id))
            lost(old);
        p->attached = true;
        engaged = true;
        p->mediaVersion = m["mediaVersion"].toInt();
        p->assetVersion = m["assetVersion"].toInt();
        const bool requested = m["startPlayback"].toBool();
        if ((requested && !initial["queue"].toArray().isEmpty() && transferId.isEmpty()) || state.isEmpty()) {
            state = initial;
            state["sessionId"] = token();
            state["rendererId"] = p->id;
            state["epoch"] = 1;
            state["revision"] = 1;
            state["queueRevision"] = 1;
            persist();
        }
        emit availabilityChanged();
        emit changed(state);
        broadcast();
        publishLibrary(p);
        if (requested && !state["queue"].toArray().isEmpty())
            transfer(id);
        return;
    }
    if (type == "ping")
        send(p, {{"type", "pong"}});
    else if (type == "rendererState") {
        auto s = m["state"].toObject();
        const auto value = m["rendererSequence"].toDouble(-1);
        if (value < 0 || value > 9e15 || std::floor(value) != value) {
            lost(p);
            return;
        }
        const auto sequence = qint64(value);
        if (transferId.isEmpty() && sequence > p->rendererSequence &&
            state["rendererId"].toString() == p->id && sessionEnvelope(s, state) &&
            normalizeSession(s, state)) {
            p->rendererSequence = sequence;
            update(s);
        }
    } else if (type == "command") {
        const auto commandId = m["commandId"].toString();
        const bool valid = sessionEnvelope(m, state) && transferId.isEmpty() && !commandId.isEmpty() &&
                           commandId.size() <= 128 &&
                           (state["rendererId"].toString() == id || peer(state["rendererId"].toString()));
        if (valid && !p->commands.contains(commandId)) {
            p->commands.insert(commandId);
            p->commandOrder.append(commandId);
            if (p->commandOrder.size() > 512)
                p->commands.remove(p->commandOrder.takeFirst());
            command(m["command"].toObject());
        }
        send(p, {{"type", "ack"}, {"commandId", commandId}, {"ok", valid}});
    } else if (type == "transfer" && sessionEnvelope(m, state)) {
        const auto target = m["target"].toString();
        if (target == id || target == p->id)
            transfer(target);
    } else if ((type == "ready" || type == "failed" || type == "released") && sessionEnvelope(m, state) &&
               !transferId.isEmpty() && m["transferId"].toString() == transferId) {
        if (stage == "preparing" && p->id == destination)
            prepared(transferId, type == "ready");
        else if (stage == "releasing" && p->id == source && type == "released")
            released(transferId, m["state"].toObject());
    } else if (type == "library") {
        if (library->merge(m["snapshot"].toObject()))
            send(p, {{"type", "libraryAck"}, {"exchangeId", m["exchangeId"]}});
    } else if (type == "libraryAck") {
        if (m["exchangeId"].toString() == p->exchange)
            p->exchange.clear();
    } else if (type == "mediaRequest" || type == "mediaCancel")
        emit mediaRequest(p->id, m);
    else if (type == "mediaReply")
        emit mediaReply(p->id, m);
}
void ConnectHost::update(QJsonObject s) {
    const bool queueChanged = s["queue"] != state["queue"] || s["order"] != state["order"];
    s["sessionId"] = state["sessionId"];
    s["rendererId"] = state["rendererId"];
    s["epoch"] = state["epoch"];
    s["revision"] = state["revision"].toDouble() + 1;
    s["queueRevision"] = state["queueRevision"].toDouble() + int(queueChanged);
    state = s;
    emit changed(state);
    broadcast();
}
void ConnectHost::broadcast() {
    for (auto p : peers)
        if (p->attached) {
            auto s = state;
            if (p->queueRevision == qint64(state["queueRevision"].toDouble()) &&
                p->session == state["sessionId"].toString()) {
                s.remove("queue");
                s.remove("order");
            }
            send(p, {{"type", "state"}, {"hostId", id}, {"state", s}});
            p->queueRevision = qint64(state["queueRevision"].toDouble());
            p->session = state["sessionId"].toString();
        }
}
bool ConnectHost::persist() {
    if (state.isEmpty())
        return true;
    auto s = state;
    s["coordinatorId"] = id;
    QSaveFile file(library->directory() + "/connect/session.json");
    const auto bytes = QJsonDocument(s).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly)) {
        emit error("Could not save the Connect session.");
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        emit error("Could not save the Connect session.");
        return false;
    }
    return true;
}
void ConnectHost::publishLibrary(ConnectPeer *p) {
    const auto snapshot = library->snapshot();
    if (snapshot == publishedLibrary && p->exchange.isEmpty() && p->property("librarySent").toBool())
        return;
    // Keep the exchange ID stable while retrying the same snapshot.
    if (snapshot != publishedLibrary) {
        publishedLibrary = snapshot;
        for (auto other : peers)
            other->setProperty("librarySent", false);
    }
    if (!p->property("librarySent").toBool())
        p->exchange = token();
    p->setProperty("librarySent", true);
    send(p, {{"type", "library"}, {"exchangeId", p->exchange}, {"snapshot", snapshot}});
}
void ConnectHost::transfer(QString target) {
    if (!transferId.isEmpty() || target == state["rendererId"].toString() || state["index"].toInt(-1) < 0 ||
        (target != id && !peer(target)))
        return;
    source = state["rendererId"].toString();
    if (source != id && !peer(source)) {
        if (target != id) {
            emit error("Reconnect the current playback device before transferring.");
            return;
        }
        // An explicit choice can recover the last paused checkpoint after its renderer left.
        const auto previous = state;
        state["playing"] = false;
        state["rendererId"] = id;
        state["epoch"] = state["epoch"].toDouble() + 1;
        state["revision"] = state["revision"].toDouble() + 1;
        if (!persist()) {
            state = previous;
            return;
        }
        emit changed(state);
        broadcast();
        return;
    }
    destination = target;
    transferId = token();
    before = state;
    releaseSent = false;
    stage = "preparing";
    transferTimer.start(150000); // A phone may need to finish its complete cache first.
    for (auto p : peers)
        if (p->attached) {
            auto m = envelope("transferProgress");
            m["target"] = target;
            m["stage"] = "preparing";
            send(p, m);
        }
    if (target == id)
        emit prepareRequested(transferId, before);
    else {
        auto m = envelope("prepare");
        m["transferId"] = transferId;
        m["state"] = before;
        send(peer(target), m);
    }
}
void ConnectHost::prepared(QString transaction, bool ok) {
    if (transaction != transferId || stage != "preparing")
        return;
    if (!ok) {
        failTransfer();
        return;
    }
    stage = "releasing";
    releaseSent = true;
    transferTimer.start(8000);
    if (source == id)
        emit releaseRequested(transferId);
    else if (auto p = peer(source)) {
        auto m = envelope("release");
        m["transferId"] = transferId;
        send(p, m);
    } else
        failTransfer();
}
void ConnectHost::released(QString transaction, QJsonObject final) {
    if (transaction != transferId || stage != "releasing")
        return;
    if (!normalizeSession(final, before) || final["queue"] != before["queue"] ||
        final["index"] != before["index"]) {
        failTransfer();
        return;
    }
    final["sessionId"] = state["sessionId"];
    final["rendererId"] = destination;
    final["epoch"] = state["epoch"].toDouble() + 1;
    final["revision"] = state["revision"].toDouble() + 1;
    final["queueRevision"] = state["queueRevision"];
    const auto previous = state;
    state = final;
    if (!persist()) {
        state = previous;
        failTransfer();
        return;
    }
    transferTimer.stop();
    transferId.clear();
    stage.clear();
    emit changed(state);
    broadcast();
}
void ConnectHost::failTransfer() {
    if (transferId.isEmpty())
        return;
    const auto transaction = transferId;
    const auto target = destination;
    transferTimer.stop();
    transferId.clear();
    stage.clear();
    if (target == id)
        emit abortRequested(transaction);
    else if (auto p = peer(target)) {
        auto m = envelope("abort");
        m["transferId"] = transaction;
        send(p, m);
    }
    for (auto p : peers)
        if (p->attached) {
            auto m = envelope("transferFailed");
            m["pauseSource"] = releaseSent;
            send(p, m);
        }
    if (releaseSent) {
        auto s = state;
        s["playing"] = false;
        update(s);
        persist();
    }
    emit error(releaseSent ? "Playback transfer failed. Playback is paused; reconnect and resume."
                           : "Playback transfer failed. The current output is retained.");
}
void ConnectHost::localState(QJsonObject snapshot) {
    if (!transferId.isEmpty() || (active() && state["rendererId"].toString() != id) ||
        !normalizeSession(snapshot))
        return;
    if (state.isEmpty()) {
        state = snapshot;
        state["sessionId"] = token();
        state["rendererId"] = id;
        state["epoch"] = 1;
        state["revision"] = 1;
        state["queueRevision"] = 1;
    } else if (!active()) {
        if (!snapshot["queue"].toArray().isEmpty()) {
            snapshot["sessionId"] = state["sessionId"];
            snapshot["rendererId"] = id;
            snapshot["epoch"] = state["epoch"];
            snapshot["revision"] = state["revision"].toDouble() + 1;
            snapshot["queueRevision"] = state["queueRevision"].toDouble() + 1;
            state = snapshot;
        }
    } else
        update(snapshot);
}
void ConnectHost::command(QJsonObject c) {
    if (!transferId.isEmpty()) {
        emit error("Wait for playback transfer to finish.");
        return;
    }
    if (state["rendererId"].toString() == id)
        emit commandRequested(c);
    else if (auto p = peer(state["rendererId"].toString())) {
        auto m = envelope("command");
        m["commandId"] = token();
        m["command"] = c;
        send(p, m);
    } else if (c["name"].toString() == "resume" || c["name"].toString() == "replace" ||
               c["name"].toString() == "next" || c["name"].toString() == "previous") {
        transfer(id);
        if (state["rendererId"].toString() == id)
            emit commandRequested(c);
    } else
        emit error("The current playback device is offline.");
}
QJsonArray ConnectHost::peerInfo() const {
    QJsonArray result;
    for (auto p : peers)
        if (p->attached)
            result.append(
                QJsonObject{{"id", p->id}, {"name", p->name}, {"renderer", state["rendererId"] == p->id}});
    return result;
}
QStringList ConnectHost::providers(bool assets) const {
    QStringList result;
    for (auto p : peers)
        if (p->attached && (assets ? p->assetVersion == 1 : p->mediaVersion == 1))
            result.append(p->id);
    return result;
}
void ConnectHost::sendMedia(QString identity, QJsonObject m) {
    if (auto p = peer(identity))
        send(p, m);
}
void ConnectHost::lost(ConnectPeer *p) {
    if (!peers.removeOne(p))
        return;
    const auto identity = p->id;
    p->socket.disconnect(this);
    p->watchdog.stop();
    p->socket.abort();
    p->deleteLater();
    emit peerLost(identity);
    if (!transferId.isEmpty() && (source == identity || destination == identity))
        failTransfer();
    if (state["rendererId"].toString() == identity) {
        auto s = state;
        s["playing"] = false;
        update(s);
        persist();
    }
    emit availabilityChanged();
}
} // namespace velune
