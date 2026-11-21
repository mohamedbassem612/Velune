#include "core/Library.h"
#include "services/ConnectClient.h"
#include "services/Discovery.h"
#include "ui/MainWindow.h"
#include <QFile>
#include <QJsonDocument>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>
using namespace velune;
namespace {
QJsonObject snapshot(QString device, QJsonArray likes = {}, QJsonArray playlists = {},
                     QJsonArray history = {}) {
    return {{"schema", 1},    {"device", device},       {"counter", 10},
            {"likes", likes}, {"playlists", playlists}, {"history", history}};
}
QJsonObject like(QString id, bool value, int version, QString device = "peer") {
    return {{"track", QJsonObject{{"id", id}, {"title", id}, {"artists", QJsonArray{"Artist"}}}},
            {"liked", value},
            {"version", QJsonObject{{"counter", version}, {"device", device}}}};
}
class TlsServer : public QTcpServer {
  public:
    QSslCertificate cert;
    QSslKey key;
    QSslSocket *peer = nullptr;
    void incomingConnection(qintptr descriptor) override {
        peer = new QSslSocket(this);
        peer->setLocalCertificate(cert);
        peer->setPrivateKey(key);
        peer->setPeerVerifyMode(QSslSocket::QueryPeer);
        peer->setProtocol(QSsl::TlsV1_2OrLater);
        peer->setSocketDescriptor(descriptor);
        QObject::connect(peer, &QSslSocket::sslErrors, peer,
                         [this](const auto &e) { peer->ignoreSslErrors(e); });
        peer->startServerEncryption();
    }
};
} // namespace
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void failedSaveDoesNotCommitMerge() {
        QTemporaryDir dir;
        QFile blocker(dir.path() + "/blocked");
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        Library library(blocker.fileName() + "/child");
        QSignalSpy errors(&library, &Library::error);
        QVERIFY(!library.merge(snapshot("peer", {like("song", true, 2)})));
        QVERIFY(!errors.isEmpty());
        QVERIFY(!library.liked("song"));
    }
    void frames() {
        QByteArray bytes = "{\"seq\":1,\"type\":\"ping\"}\n{\"seq\":2}\n";
        qint64 seq = 0;
        QJsonObject m;
        QString error;
        QVERIFY(ConnectClient::decodeFrame(bytes, seq, m, error));
        QCOMPARE(m["type"].toString(), "ping");
        QVERIFY(ConnectClient::decodeFrame(bytes, seq, m, error));
        QCOMPARE(seq, 2);
        QVERIFY(bytes.isEmpty());
    }
    void rejectsReplay() {
        QByteArray bytes = "{\"seq\":1}\n";
        qint64 seq = 1;
        QJsonObject m;
        QString error;
        QVERIFY(!ConnectClient::decodeFrame(bytes, seq, m, error));
        QVERIFY(!error.isEmpty());
    }
    void partialAndOversizedFrames() {
        QByteArray b = "{\"seq\":";
        qint64 seq = 0;
        QJsonObject m;
        QString e;
        QVERIFY(!ConnectClient::decodeFrame(b, seq, m, e));
        QVERIFY(e.isEmpty());
        b += "1}\n";
        QVERIFY(ConnectClient::decodeFrame(b, seq, m, e));
        b = QByteArray(12 * 1024 * 1024 + 1, 'x');
        QVERIFY(!ConnectClient::decodeFrame(b, seq, m, e));
        QVERIFY(!e.isEmpty());
    }
    void localPeersOnly() {
        QVERIFY(ConnectClient::localAddress(QHostAddress("192.168.1.2")));
        QVERIFY(ConnectClient::localAddress(QHostAddress("172.16.0.1")));
        QVERIFY(ConnectClient::localAddress(QHostAddress("10.0.0.1")));
        QVERIFY(ConnectClient::localAddress(QHostAddress("::1")));
        QVERIFY(ConnectClient::localAddress(QHostAddress("fd00::1")));
        QVERIFY(!ConnectClient::localAddress(QHostAddress("8.8.8.8")));
        QVERIFY(!ConnectClient::localAddress(QHostAddress("172.32.0.1")));
        QVERIFY(!ConnectClient::localAddress(QHostAddress("2001:4860::8888")));
    }
    void libraryTombstonesAndIdempotence() {
        QTemporaryDir dir;
        Library l(dir.path());
        QSignalSpy changed(&l, &Library::changed);
        QVERIFY(l.merge(snapshot("a", {like("song", true, 2)})));
        QVERIFY(l.liked("song"));
        QVERIFY(l.merge(snapshot("b", {like("song", false, 3)})));
        QVERIFY(!l.liked("song"));
        auto count = changed.size();
        QVERIFY(l.merge(snapshot("a", {like("song", true, 2)})));
        QVERIFY(!l.liked("song"));
        QCOMPARE(changed.size(), count);
        Library restored(dir.path());
        QVERIFY(!restored.liked("song"));
        QCOMPARE(restored.deviceId(), l.deviceId());
        QVERIFY(!l.merge({{"schema", 7}}));
    }
    void tieBreakAndHistoryDeletion() {
        QTemporaryDir dir;
        Library l(dir.path());
        QVERIFY(l.merge(snapshot("a", {like("song", true, 3, "a")})));
        QVERIFY(l.merge(snapshot("b", {like("song", false, 3, "b")})));
        QVERIFY(!l.liked("song"));
        QJsonObject h{{"track", QJsonObject{{"id", "song"}}},
                      {"timestamp", "2026-11-20T12:00:00Z"},
                      {"playTime", 100},
                      {"version", QJsonObject{{"counter", 5}, {"device", "a"}}},
                      {"removed", false}};
        QVERIFY(l.merge(snapshot("a", {}, {}, {h})));
        h["removed"] = true;
        QVERIFY(l.merge(snapshot("a", {}, {}, {h})));
        h["removed"] = false;
        QVERIFY(l.merge(snapshot("a", {}, {}, {h})));
        QVERIFY(l.snapshot()["history"].toArray()[0].toObject()["removed"].toBool());
    }
    void identityPersistence() {
        QTemporaryDir dir;
        Library l(dir.path());
        ConnectClient first(&l);
        QVERIFY(first.identity().size() == 64);
        ConnectClient second(&l);
        QCOMPARE(first.identity(), second.identity());
        QFile f(dir.path() + "/connect/identity-key.pem");
        QVERIFY(!(f.permissions() & QFile::ReadOther));
        QVERIFY(!(f.permissions() & QFile::ReadGroup));
    }
    void tlsHandshakeCommandsAndMedia() {
        QTemporaryDir dir;
        Library l(dir.path() + "/client"), serverLibrary(dir.path() + "/server");
        ConnectClient identity(&serverLibrary);
        QFile certFile(serverLibrary.directory() + "/connect/identity.pem"),
            keyFile(serverLibrary.directory() + "/connect/identity-key.pem");
        QVERIFY(certFile.open(QIODevice::ReadOnly));
        QVERIFY(keyFile.open(QIODevice::ReadOnly));
        TlsServer server;
        server.cert = QSslCertificate(certFile.readAll());
        server.key = QSslKey(keyFile.readAll(), QSsl::Rsa);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        ConnectClient client(&l);
        QSignalSpy states(&client, &ConnectClient::stateChanged);
        QSignalSpy errors(&client, &ConnectClient::error);
        client.attach("127.0.0.1", server.serverPort(), identity.identity());
        QTRY_VERIFY_WITH_TIMEOUT(server.peer && server.peer->isEncrypted(), 10000);
        QTRY_VERIFY(server.peer->canReadLine());
        auto hello = QJsonDocument::fromJson(server.peer->readLine()).object();
        QCOMPARE(hello["type"].toString(), "identity");
        QCOMPARE(hello["id"].toString(), client.identity());
        qint64 seq = 0;
        auto send = [&](QJsonObject o) {
            o["seq"] = double(++seq);
            server.peer->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        };
        send({{"type", "identity"}, {"id", identity.identity()}, {"protocol", 4}, {"name", "Test TV"}});
        QTRY_VERIFY(server.peer->canReadLine());
        auto attached = QJsonDocument::fromJson(server.peer->readLine()).object();
        QCOMPARE(attached["type"].toString(), "connect");
        QVERIFY(!attached["startPlayback"].toBool());
        QVERIFY(!attached.contains("mediaVersion"));
        QJsonObject state{{"sessionId", "session"}, {"epoch", 3},
                          {"revision", 4},          {"queue", QJsonArray{}},
                          {"order", QJsonArray{}},  {"playing", false}};
        send({{"type", "state"}, {"hostId", identity.identity()}, {"state", state}});
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(server.peer->canReadLine());
        auto initialLibrary = QJsonDocument::fromJson(server.peer->readLine()).object();
        QCOMPARE(initialLibrary["type"].toString(), "library");
        send({{"type", "libraryAck"}, {"exchangeId", initialLibrary["exchangeId"]}});
        client.command({{"name", "resume"}});
        QTRY_VERIFY(server.peer->canReadLine());
        auto command = QJsonDocument::fromJson(server.peer->readLine()).object();
        QCOMPARE(command["sessionId"].toString(), "session");
        QCOMPARE(command["epoch"].toInt(), 3);
        QCOMPARE(command["command"].toObject()["name"].toString(), "resume");
        send({{"type", "ack"}, {"commandId", command["commandId"]}, {"ok", true}});
        state["revision"] = 3;
        state["playing"] = true;
        send({{"type", "state"}, {"hostId", identity.identity()}, {"state", state}});
        QTest::qWait(100);
        QVERIFY(!client.playback()["playing"].toBool());
        QFile audio(dir.path() + "/audio");
        QVERIFY(audio.open(QIODevice::WriteOnly));
        audio.write("audio-bytes");
        audio.close();
        l.registerMedia({"song", "Song", "Artist", {}, 0}, audio.fileName());
        send({{"type", "mediaRequest"},
              {"requestId", "open"},
              {"operation", "open"},
              {"song", "song"},
              {"position", 2},
              {"length", 4},
              {"cachedOnly", true}});
        QJsonObject reply;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     if (!reply.isEmpty())
                                         return true;
                                     while (server.peer->canReadLine()) {
                                         auto o = QJsonDocument::fromJson(server.peer->readLine()).object();
                                         if (o["type"] == "mediaReply") {
                                             reply = o;
                                             return true;
                                         }
                                     }
                                     return false;
                                 })(),
                                 3000);
        QVERIFY(reply["ok"].toBool());
        auto handle = reply["handle"].toString();
        QCOMPARE(reply["length"].toInt(), 4);
        send({{"type", "mediaRequest"}, {"requestId", "read"}, {"operation", "read"}, {"handle", handle}});
        QTRY_VERIFY(server.peer->canReadLine());
        reply = QJsonDocument::fromJson(server.peer->readLine()).object();
        QCOMPARE(QByteArray::fromBase64(reply["data"].toString().toLatin1()), QByteArray("dio-"));
        send({{"type", "mediaRequest"}, {"requestId", "eof"}, {"operation", "read"}, {"handle", handle}});
        QTRY_VERIFY(server.peer->canReadLine());
        reply = QJsonDocument::fromJson(server.peer->readLine()).object();
        QVERIFY(reply["eof"].toBool());
        send({{"type", "mediaCancel"}, {"requestId", "open"}});
        send({{"type", "mediaRequest"},
              {"requestId", "afterCancel"},
              {"operation", "read"},
              {"handle", handle}});
        QTRY_VERIFY(server.peer->canReadLine());
        reply = QJsonDocument::fromJson(server.peer->readLine()).object();
        QVERIFY(!reply["ok"].toBool());
        client.detach();
        QVERIFY(!client.connected());
    }
    void layoutAndLoginGate() {
        QTemporaryDir dir;
        Library l(dir.path());
        ConnectClient c(&l);
        MusicService m(&l, &c);
        Discovery d;
        MainWindow w(&l, &m, &c, &d);
        w.show();
        QTest::qWait(80);
        auto gate = w.findChild<QWidget *>("loginGate");
        QVERIFY(gate->isVisible());
        auto tile = w.findChild<QuickTile *>("likedTile");
        QVERIFY(tile);
        QCOMPARE(tile->text(), "Liked");
        QCOMPARE(w.findChildren<QuickTile *>().size(), 9);
        QCOMPARE(w.findChildren<SongCard *>().size(), 3);
        w.setPreview(true);
        QVERIFY(!gate->isVisible());
        for (auto size : {QSize(1660, 938), QSize(1280, 714), QSize(1000, 680)}) {
            w.resize(size);
            QTest::qWait(40);
            auto player = w.findChild<QWidget *>("bottomPlayer");
            if (!qEnvironmentVariable("VELUNE_TEST_SCREENSHOTS").isEmpty()) {
                QDir().mkpath(qEnvironmentVariable("VELUNE_TEST_SCREENSHOTS"));
                w.grab().save(qEnvironmentVariable("VELUNE_TEST_SCREENSHOTS") + "/home-" +
                              QString::number(size.width()) + ".png");
            }
            QCOMPARE(player->height(), 128);
            QCOMPARE(player->width(), w.width() - 2);
            auto seek = w.findChild<QSlider *>("seekBar");
            auto play = w.findChild<IconButton *>("playPause");
            QVERIFY(seek->mapTo(&w, QPoint()).y() > play->mapTo(&w, QPoint()).y());
            QVERIFY(qAbs(tile->width() - w.findChild<QuickTile *>("quick1")->width()) <= 1);
            QVERIFY(player->geometry().bottom() <= w.height());
        }
    }
};
QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
