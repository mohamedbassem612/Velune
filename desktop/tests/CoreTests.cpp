#include "FakePlaybackEngine.h"
#include "core/Library.h"
#include "services/ConnectClient.h"
#include "services/Discovery.h"
#include "ui/MainWindow.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QScrollArea>
#include <QScrollBar>
#include <QSslSocket>
#include <QStyledItemDelegate>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#ifdef VELUNE_MULTIMEDIA
#include <QAudioDevice>
#include <QMediaDevices>
#endif
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
        auto engine = std::make_unique<FakePlaybackEngine>();
        auto *localEngine = engine.get();
        MusicService music(&l, &client, std::move(engine));
        QFile localAudio(dir.path() + "/local-audio");
        QVERIFY(localAudio.open(QIODevice::WriteOnly));
        localAudio.write("fixture");
        localAudio.close();
        Track localTrack{"local:routing", "Local", "Test", {}, 20};
        l.registerMedia(localTrack, localAudio.fileName());
        music.play({localTrack});
        QVERIFY(music.playback()["playing"].toBool());
        QSignalSpy states(&client, &ConnectClient::stateChanged);
        QSignalSpy errors(&client, &ConnectClient::error);
        client.attach("127.0.0.1", server.serverPort(), identity.identity());
        QVERIFY(localEngine->source().isEmpty());
        QVERIFY(!music.playback()["playing"].toBool());
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
        music.action("resume");
        QVERIFY(localEngine->source().isEmpty());
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
        QVERIFY(!music.playback()["playing"].toBool());
        QVERIFY(localEngine->source().isEmpty());
        QCOMPARE(music.playback()["song"].toObject()["id"].toString(), localTrack.id);
    }
    void seekingWhilePlayingThroughTheUi() {
#ifdef VELUNE_MULTIMEDIA
        if (QMediaDevices::audioOutputs().isEmpty())
            QSKIP(
                "Native audio UI check requires an output device; Linux CI supplies an isolated null sink.");
        QTemporaryDir directory;
        QByteArray wav(44 + 160000, char(128));
        wav.replace(0, 4, "RIFF");
        qToLittleEndian<quint32>(wav.size() - 8, wav.data() + 4);
        wav.replace(8, 8, "WAVEfmt ");
        qToLittleEndian<quint32>(16, wav.data() + 16);
        qToLittleEndian<quint16>(1, wav.data() + 20);
        qToLittleEndian<quint16>(1, wav.data() + 22);
        qToLittleEndian<quint32>(8000, wav.data() + 24);
        qToLittleEndian<quint32>(8000, wav.data() + 28);
        qToLittleEndian<quint16>(1, wav.data() + 32);
        qToLittleEndian<quint16>(8, wav.data() + 34);
        wav.replace(36, 4, "data");
        qToLittleEndian<quint32>(160000, wav.data() + 40);
        QFile file(directory.filePath("silence.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(wav), wav.size());
        file.close();
        Library library(directory.path());
        Track track{"local:seek", "Seek test", "Test", {}, 20};
        library.registerMedia(track, file.fileName());
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.resize(1280, 714);
        window.show();
        music.volume(0);
        music.play({track});
        QTRY_VERIFY_WITH_TIMEOUT(music.playback()["position"].toDouble() > 100, 5000);
        auto slider = window.findChild<QSlider *>("seekBar");
        QVERIFY(slider);
        QTRY_COMPARE(slider->maximum(), 20000);
        QTest::mouseClick(slider, Qt::LeftButton, {}, QPoint(slider->width() * 3 / 4, slider->height() / 2));
        QTest::qWait(600);
        QVERIFY(music.playback()["position"].toDouble() > 14000);
        QVERIFY(music.playback()["playing"].toBool());
        QTest::mousePress(slider, Qt::LeftButton, {}, QPoint(slider->width() * 3 / 4, slider->height() / 2));
        QTest::mouseMove(slider, QPoint(slider->width() / 4, slider->height() / 2));
        QTest::qWait(300); // Playback updates must not overwrite the dragged position.
        QTest::mouseRelease(slider, Qt::LeftButton, {}, QPoint(slider->width() / 4, slider->height() / 2));
        QTest::qWait(600);
        auto position = music.playback()["position"].toDouble();
        QVERIFY(position > 4000 && position < 7000);
        QTest::keyClick(slider, Qt::Key_Right);
        QTest::qWait(600);
        QVERIFY(music.playback()["position"].toDouble() > 9000);
        QVERIFY(music.playback()["playing"].toBool());
#endif
    }
    void contentButtonsSeparateNavigationAndPlayback() {
        QuickTile tile("Test collection", Icon::Heart);
        tile.resize(380, 76);
        tile.show();
        QSignalSpy opened(&tile, &QPushButton::clicked);
        QSignalSpy played(&tile, &QuickTile::playRequested);
        QTest::mouseMove(&tile, QPoint(20, 20));
        QTRY_VERIFY(tile.playButton()->isVisible());
        QTest::mouseClick(tile.playButton(), Qt::LeftButton);
        QCOMPARE(played.size(), 1);
        QCOMPARE(opened.size(), 0);
        QTest::mouseClick(&tile, Qt::LeftButton, {}, QPoint(120, 30));
        QCOMPARE(opened.size(), 1);
        QCOMPARE(played.size(), 1);
        QEvent leave(QEvent::Leave);
        tile.clearFocus();
        tile.playButton()->clearFocus();
        QApplication::sendEvent(&tile, &leave);
        QVERIFY(!tile.playButton()->isVisible());
        SongCard card(0);
        card.resize(380, 290);
        card.show();
        QSignalSpy cardOpened(&card, &QPushButton::clicked);
        QSignalSpy cardPlayed(&card, &SongCard::playRequested);
        QTest::mouseMove(&card, QPoint(20, 20));
        QTRY_VERIFY(card.playButton()->isVisible());
        QTest::mouseClick(card.playButton(), Qt::LeftButton);
        QCOMPARE(cardPlayed.size(), 1);
        QCOMPARE(cardOpened.size(), 0);
        QTest::mouseClick(&card, Qt::LeftButton, {}, QPoint(120, 30));
        QCOMPARE(cardOpened.size(), 1);
        QCOMPARE(cardPlayed.size(), 1);
    }
    void openingCollectionsDoesNotStartPlayback() {
        QTemporaryDir dir;
        Library library(dir.path());
        Track first{"one", "First song", "First artist", {}, 201};
        Track second{"two", "Second song", "Second artist", {}, 210};
        library.importTracks({first, second});
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        QSignalSpy playback(&music, &MusicService::playbackChanged);
        auto liked = window.findChild<QuickTile *>("likedTile");
        QTest::mouseClick(liked, Qt::LeftButton, {}, QPoint(120, 30));
        auto pages = window.findChild<QStackedWidget *>("pages");
        QCOMPARE(pages->currentWidget()->objectName(), "collectionPage");
        QCOMPARE(window.findChild<QLabel *>("collectionTitle")->text(), "Liked Songs");
        QCOMPARE(window.findChild<QLabel *>("collectionCount")->text(), "2 songs");
        QCOMPARE(playback.size(), 0);
        auto rowBody = pages->currentWidget()->findChild<QuickTile *>("collectionTrack");
        QVERIFY(rowBody);
        QTest::mouseClick(rowBody, Qt::LeftButton, {}, QPoint(120, 30));
        QCOMPARE(window.findChild<QLabel *>("collectionTitle")->text(), "First song");
        QCOMPARE(playback.size(), 0);
        QTest::mouseClick(window.findChild<IconButton *>("collectionBack"), Qt::LeftButton);
        QCOMPARE(window.findChild<QLabel *>("collectionTitle")->text(), "Liked Songs");
        auto heart = pages->currentWidget()->findChildren<IconButton *>();
        for (auto button : heart) {
            if (button->property("trackId").toString() == "one") {
                QTest::mouseClick(button, Qt::LeftButton);
                break;
            }
        }
        QCOMPARE(library.tracks(true).size(), 1);
        QCOMPARE(window.findChild<QLabel *>("collectionCount")->text(), "1 song");
        QTest::mouseClick(window.findChild<IconButton *>("collectionBack"), Qt::LeftButton);
        QCOMPARE(pages->currentIndex(), 0);
        QTest::mouseClick(window.findChild<QuickTile *>("quick1"), Qt::LeftButton, {}, QPoint(120, 30));
        QCOMPARE(window.findChild<QLabel *>("collectionTitle")->text(), "Daily Mix 1");
        QCOMPARE(playback.size(), 0);
        QTest::qWait(100);
        if (!qEnvironmentVariable("VELUNE_TEST_SCREENSHOTS").isEmpty()) {
            QDir().mkpath(qEnvironmentVariable("VELUNE_TEST_SCREENSHOTS"));
            window.grab().save(qEnvironmentVariable("VELUNE_TEST_SCREENSHOTS") + "/daily-mix.png");
        }
    }
    void heartShowsPersistedLikeState() {
        IconButton heart(Icon::Heart, "Like song");
        heart.setCheckable(true);
        heart.show();
        QTest::qWait(30);
        const auto center = heart.rect().center();
        QVERIFY(heart.grab().toImage().pixelColor(center).lightness() < 100);
        QTest::mouseClick(&heart, Qt::LeftButton);
        QVERIFY(heart.isChecked());
        QTest::qWait(240);
        QVERIFY(heart.grab().toImage().pixelColor(center).lightness() > 220);
        QTest::mouseClick(&heart, Qt::LeftButton);
        QVERIFY(!heart.isChecked());
        QTest::qWait(240);
        QVERIFY(heart.grab().toImage().pixelColor(center).lightness() < 100);
        heart.setChecked(true);
        QTest::qWait(220);
        {
            QSignalBlocker block(&heart);
            heart.setChecked(false);
        }
        QVERIFY(!heart.isChecked());
        QVERIFY(heart.grab().toImage().pixelColor(center).lightness() < 100);
    }
    void likeButtonsFollowLibraryMembershipAcrossViewsAndTrackChanges() {
        QTemporaryDir dir;
        Library library(dir.path());
        Track song{"song", "Song", "Artist", {}, 180};
        Track other{"other", "Other song", "Artist", {}, 180};
        library.importTracks({song}, false); // Being in the library is not being liked.
        LikeButton first(&library, song), mirror(&library, song);
        first.show();
        mirror.show();
        QTest::qWait(30);
        const auto filled = [](IconButton *button) {
            return button->grab().toImage().pixelColor(button->rect().center()).lightness() > 200;
        };
        QVERIFY(!first.isChecked());
        QVERIFY(!mirror.isChecked());
        QVERIFY(!filled(&first));
        first.setChecked(true); // Transient widget state cannot override library membership.
        QVERIFY(!filled(&first));
        QVERIFY(!first.isChecked());
        QTest::mouseMove(&first, first.rect().center());
        QTest::qWait(200);
        QVERIFY(!filled(&first));
        QTest::mouseClick(&first, Qt::LeftButton);
        QTest::qWait(220);
        QVERIFY(library.liked(song.id));
        QVERIFY(first.isChecked());
        QVERIFY(mirror.isChecked());
        QVERIFY(filled(&first));
        QVERIFY(filled(&mirror));
        QCOMPARE(library.tracks(true).size(), 1);
        QTest::mouseClick(&mirror, Qt::LeftButton);
        QTest::qWait(220);
        QVERIFY(!library.liked(song.id));
        QVERIFY(!first.isChecked());
        QVERIFY(!mirror.isChecked());
        QVERIFY(!filled(&first));
        QVERIFY(!filled(&mirror));
        QVERIFY(library.tracks(true).isEmpty());
        library.setLiked(song, true);
        QTest::qWait(220);
        QVERIFY(filled(&first));
        first.setTrack(other);
        QVERIFY(!first.isChecked());
        QVERIFY(!filled(&first));
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        QTest::qWait(30);
        auto playerLike = window.findChild<LikeButton *>("playerLike");
        QVERIFY(playerLike);
        music.playbackChanged({{"song", song.wire()}});
        QVERIFY(playerLike->isChecked());
        QVERIFY(filled(playerLike));
        music.playbackChanged({{"song", other.wire()}});
        QVERIFY(!playerLike->isChecked());
        QVERIFY(!filled(playerLike));
        library.setLiked(other, true);
        QTest::qWait(220);
        QVERIFY(playerLike->isChecked());
        QVERIFY(filled(playerLike));
        library.setLiked(other, false);
        QTest::qWait(220);
        QVERIFY(!playerLike->isChecked());
        QVERIFY(!filled(playerLike));
    }
    void failedLikeSaveCannotFillTheHeart() {
        QTemporaryDir dir;
        const auto root = dir.filePath("library");
        Library library(root);
        Track song{"song", "Song", "Artist", {}, 180};
        LikeButton heart(&library, song);
        heart.show();
        QVERIFY(QDir(root).removeRecursively());
        QFile blocked(root);
        QVERIFY(blocked.open(QIODevice::WriteOnly));
        blocked.close();
        QSignalSpy error(&library, &Library::error);
        QTest::mouseClick(&heart, Qt::LeftButton);
        QTest::qWait(220);
        QCOMPARE(error.size(), 1);
        QVERIFY(!library.liked(song.id));
        QVERIFY(!heart.isChecked());
        QVERIFY(heart.grab().toImage().pixelColor(heart.rect().center()).lightness() < 100);
    }
    void likingTracksKeepsScrollPositionAndUpdatesHeart() {
        QTemporaryDir dir;
        Library library(dir.path());
        QList<Track> tracks;
        for (int i = 0; i < 70; ++i)
            tracks << Track{
                QString("track%1").arg(i, 3, 10, QChar('0')), QString("Song %1").arg(i), "Artist", {}, 180};
        library.importTracks(tracks, false);
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        window.findChild<QuickTile *>("quick1")->click();
        QTest::qWait(60);
        auto page = window.findChild<QWidget *>("collectionPage");
        auto scroll = page->findChild<QScrollArea *>();
        auto bar = scroll->verticalScrollBar();
        QVERIFY(bar->maximum() > 1500);
        bar->setValue(1050);
        QTest::qWait(30);
        IconButton *heart = nullptr;
        for (auto button : page->findChildren<IconButton *>())
            if (button->isCheckable() && button->isVisible() &&
                QRect(button->mapTo(scroll->viewport(), QPoint()), button->size())
                    .intersects(scroll->viewport()->rect())) {
                heart = button;
                break;
            }
        QVERIFY(heart);
        QVERIFY(!heart->isChecked());
        const auto before = bar->value();
        QPointer<IconButton> original = heart;
        QTest::mouseClick(heart, Qt::LeftButton);
        QTest::qWait(250);
        QVERIFY2(qAbs(bar->value() - before) <= 2, "Liking a track moved the playlist scroll position");
        QVERIFY2(original, "The liked row was rebuilt instead of updating its heart");
        QVERIFY(original->isChecked());
        QTest::mouseClick(original, Qt::LeftButton);
        QTest::qWait(250);
        QVERIFY(original);
        QVERIFY(!original->isChecked());
        QVERIFY(qAbs(bar->value() - before) <= 2);
    }
    void removingLikedRowKeepsScrollAndOtherRows() {
        QTemporaryDir dir;
        Library library(dir.path());
        QList<Track> tracks;
        for (int i = 0; i < 70; ++i)
            tracks << Track{
                QString("track%1").arg(i, 3, 10, QChar('0')), QString("Song %1").arg(i), "Artist", {}, 180};
        library.importTracks(tracks);
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        window.findChild<QuickTile *>("likedTile")->click();
        QTest::qWait(80);
        auto page = window.findChild<QWidget *>("collectionPage");
        auto scroll = page->findChild<QScrollArea *>();
        auto bar = scroll->verticalScrollBar();
        bar->setValue(1200);
        QTest::qWait(30);
        QPointer<QuickTile> unaffected = page->findChild<QuickTile *>("collectionTrack");
        IconButton *heart = nullptr;
        for (auto button : page->findChildren<IconButton *>("likeButton"))
            if (QRect(button->mapTo(scroll->viewport(), QPoint()), button->size())
                    .intersects(scroll->viewport()->rect())) {
                heart = button;
                break;
            }
        QVERIFY(heart);
        QVERIFY(heart->isChecked());
        const auto before = bar->value();
        QTest::mouseClick(heart, Qt::LeftButton);
        QTest::qWait(250);
        QCOMPARE(library.tracks(true).size(), 69);
        QCOMPARE(window.findChild<QLabel *>("collectionCount")->text(), "69 songs");
        QVERIFY(unaffected);
        QVERIFY2(qAbs(bar->value() - before) <= 2,
                 "Removing a liked song moved the playlist scroll position");
    }
    void searchUpdatesWhileTypingAndCancelsPendingQueries() {
        QTemporaryDir dir;
        Library library(dir.path());
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        QTest::qWait(30);
        for (auto button : window.findChildren<NavButton *>())
            if (button->text() == "Search")
                button->click();
        auto input = window.findChild<QLineEdit *>("searchInput");
        auto list = window.findChild<QListWidget *>("searchResults");
        auto status = window.findChild<QLabel *>("searchStatus");
        QSignalSpy requests(&music, &MusicService::suggestionsStarted);
        QSignalSpy submitted(&music, &MusicService::searchStarted);
        QSignalSpy responses(&music, &MusicService::suggestionsReady);
        QTRY_VERIFY(input->hasFocus());
        QTest::keyClicks(input, "test");
        QTest::qWait(80);
        QTest::keyClicks(input, " song");
        QTest::qWait(80);
        QCOMPARE(requests.size(), 0);
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.last().first().toString(), "test song");
        // Clearing must cancel even an already-running request, with no late result repaint.
        input->clear();
        QCOMPARE(list->count(), 0);
        QCOMPARE(status->text(), "Type to search YouTube Music");
        const int completed = responses.size();
        QTest::qWait(250);
        QCOMPARE(responses.size(), completed);
        QCOMPARE(status->text(), "Type to search YouTube Music");
        input->setText(QString::fromUtf8("  سورة البقرة  "));
        QTRY_COMPARE(requests.size(), 2);
        QCOMPARE(requests.last().first().toString(), QString::fromUtf8("سورة البقرة"));
        input->clear();
        // Enter submits immediately and stops the scheduled automatic duplicate.
        input->setText("enter query");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(submitted.size(), 1);
        QTest::qWait(250);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(submitted.size(), 1);
        // Leaving the page must cancel a query still in the debounce window.
        input->setText("do not send");
        for (auto button : window.findChildren<NavButton *>())
            if (button->text() == "Home")
                button->click();
        QTest::qWait(250);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(submitted.size(), 1);
        input->setText("   ");
        QTest::qWait(250);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(submitted.size(), 1);
    }
    void androidLibraryCompositionAndViews() {
        QTemporaryDir dir;
        QPixmap art(180, 180);
        art.fill(QColor("#446677"));
        QVERIFY(art.save(dir.filePath("cover.png")));
        const auto url = QUrl::fromLocalFile(dir.filePath("cover.png")).toString();
        Library library(dir.path());
        Track first{"one", "First song", "Artist", url, 180, "Saved album", "MPRE-saved"};
        Track second{"two", "Second song", "Artist", url, 180};
        library.setLiked(first, true);
        library.rememberTracks({second});
        library.putPlaylist({}, "Evening playlist", {first, second});
        ConnectClient remote(&library);
        MusicService music(&library, &remote, std::make_unique<FakePlaybackEngine>());
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        auto catalog = music.libraryCatalog();
        catalog->bookmark(
            {{"id", "MPRE-saved"}, {"title", "Saved album"}, {"kind", "album"}, {"thumbnail", url}}, true);
        window.resize(1660, 980);
        window.show();
        for (auto button : window.findChildren<NavButton *>())
            if (button->text() == "Library")
                button->click();
        auto list = window.findChild<TrackListWidget *>("musicLibrary");
        QVERIFY(list);
        QTRY_COMPARE(list->count(), 6);
        QCOMPARE(list->item(0)->data(Qt::UserRole + 3).toJsonObject()["section"].toString(), "liked");
        QVERIFY(!list->item(3)->data(Qt::UserRole + 10).toBool()); // Empty cached list offers no play.
        const auto capture = qEnvironmentVariable("VELUNE_LIBRARY_CAPTURE");
        window.resize(1658, 978);
        QTest::qWait(50);
        if (!capture.isEmpty())
            QVERIFY(window.grab().save(capture + "-list.png"));
        window.findChild<QPushButton *>("libraryTab_songs")->click();
        QTRY_COMPARE(list->count(), 1);
        auto scope = window.findChild<QComboBox *>("libraryScope");
        scope->setCurrentIndex(scope->findData("library"));
        QTRY_COMPARE(list->count(), 2);
        window.findChild<QPushButton *>("libraryTab_albums")->click();
        QTRY_COMPARE(list->count(), 1);
        window.findChild<QPushButton *>("libraryTab_songs")->click();
        QTRY_COMPARE(list->count(), 2); // Per-section scope restored.
        library.setLiked(first, false);
        scope->setCurrentIndex(scope->findData("liked"));
        QTRY_COMPARE(list->count(), 0);
        window.findChild<QPushButton *>("libraryTab_all")->click();
        auto view = window.findChild<QComboBox *>("libraryView");
        view->setCurrentIndex(view->findData("grid"));
        QCOMPARE(list->viewMode(), QListView::IconMode);
        QTRY_COMPARE(list->count(), 6);
        QTest::qWait(50);
        if (!capture.isEmpty())
            QVERIFY(window.grab().save(capture + "-grid.png"));
        window.resize(1000, 720);
        QTest::qWait(30);
        auto refresh = window.findChild<QPushButton *>("libraryRefresh");
        QVERIFY(refresh->mapTo(&window, refresh->rect().bottomRight()).x() < window.width());
        if (!capture.isEmpty())
            QVERIFY(window.grab().save(capture + "-compact.png"));
        // Clicking a card opens it without starting audio.
        QSignalSpy playback(&music, &MusicService::playbackChanged);
        auto row = list->visualItemRect(list->item(0));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          row.bottomLeft() + QPoint(40, -12));
        QTRY_VERIFY(window.findChild<QWidget *>("collectionPage")->isVisible());
        QCOMPARE(playback.size(), 0);
    }
    void trackListsShowArtworkAndRejectObsoleteImages() {
        QTemporaryDir dir;
        QPixmap red(100, 60), blue(60, 100);
        red.fill(Qt::red);
        blue.fill(Qt::blue);
        QVERIFY(red.save(dir.filePath("red.png")));
        QVERIFY(blue.save(dir.filePath("blue.png")));
        Track first{"red", "Red song", "Artist", QUrl::fromLocalFile(dir.filePath("red.png")).toString(),
                    180};
        Track second{"blue", "Blue song", "Artist", QUrl::fromLocalFile(dir.filePath("blue.png")).toString(),
                     180};
        Library library(dir.path());
        library.importTracks({first, second});
        ConnectClient remote(&library);
        MusicService music(&library, &remote, std::make_unique<FakePlaybackEngine>());
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        QTest::qWait(30);
        const auto navigate = [&window](const QString &name) {
            for (auto button : window.findChildren<NavButton *>())
                if (button->text() == name)
                    button->click();
        };
        const auto colorOf = [](QListWidgetItem *item) {
            return item->icon().pixmap(56, 56).toImage().pixelColor(28, 28);
        };
        navigate("Search");
        auto list = window.findChild<QListWidget *>("searchResults");
        music.results({first});
        QTRY_COMPARE(colorOf(list->item(0)), QColor(Qt::red));
        music.results({second});
        QTRY_COMPARE(colorOf(list->item(0)), QColor(Qt::blue));
        // Start a deferred image load, then replace the result before it completes.
        QVERIFY(red.save(dir.filePath("late-red.png")));
        auto late = first;
        late.thumbnail = QUrl::fromLocalFile(dir.filePath("late-red.png")).toString();
        music.results({late});
        list->verticalScrollBar()->valueChanged(0);
        music.results({second});
        QTest::qWait(30);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), "blue");
        QCOMPARE(colorOf(list->item(0)), QColor(Qt::blue));
        music.results({});
        QTest::qWait(20);
        QCOMPARE(list->count(), 0);
        navigate("Library");
        auto libraryList = window.findChild<QListWidget *>("musicLibrary");
        QVERIFY(libraryList);
        for (auto button : window.findChildren<QPushButton *>())
            if (button->objectName() == "libraryTab_songs")
                button->click();
        auto scope = window.findChild<QComboBox *>("libraryScope");
        scope->setCurrentIndex(scope->findData("library"));
        auto sort = window.findChild<QComboBox *>("librarySort");
        sort->setCurrentIndex(sort->findData("name"));
        music.libraryCatalog()->setPreferences({{"descending", false}});
        QTRY_COMPARE(libraryList->count(), 2);
        // Alphabetical library scope includes both imported songs.
        QTRY_COMPARE(colorOf(libraryList->item(0)), QColor(Qt::blue));
        music.playbackChanged({{"song", first.wire()}, {"queue", QJsonArray{first.wire(), second.wire()}}});
        auto playerCover = window.findChild<QLabel *>("playerCover");
        QVERIFY(playerCover);
        QTRY_COMPARE(playerCover->pixmap().toImage().pixelColor(38, 38), QColor(Qt::red));
        for (auto button : window.findChildren<IconButton *>())
            if (button->toolTip() == "Queue")
                button->click();
        auto queue = window.findChild<QListWidget *>("queueTracks");
        QVERIFY(queue);
        QTRY_COMPARE(queue->count(), 2);
        QTRY_COMPARE(colorOf(queue->item(0)), QColor(Qt::red));
    }
    void listHoverShowsCoverPlayAndSeparatesClicks() {
        TrackListWidget list;
        list.resize(500, 200);
        QPixmap art(56, 56);
        art.fill(Qt::red);
        auto item = new QListWidgetItem(QIcon(art), "Song\nArtist", &list);
        item->setSizeHint(QSize(0, 88));
        list.show();
        QTest::qWait(30);
        QSignalSpy played(&list, &TrackListWidget::playRequested);
        QSignalSpy opened(&list, &TrackListWidget::openRequested);
        const auto row = list.visualItemRect(item);
        const auto cover = TrackListWidget::coverRect(row);
        QStyleOptionViewItem option;
        option.initFrom(&list);
        option.rect = row;
        option.state = QStyle::State_Enabled;
        QPixmap normal(list.viewport()->size()), hovered(list.viewport()->size());
        normal.fill(Qt::black);
        hovered.fill(Qt::black);
        {
            QPainter painter(&normal);
            list.itemDelegate()->paint(&painter, option, list.model()->index(0, 0));
        }
        option.state |= QStyle::State_MouseOver;
        {
            QPainter painter(&hovered);
            list.itemDelegate()->paint(&painter, option, list.model()->index(0, 0));
        }
        QCOMPARE(normal.toImage().pixelColor(cover.center()), QColor(Qt::red));
        QVERIFY(hovered.toImage().pixelColor(cover.center()).green() > 150);
        const auto background = QPoint(row.right() - 24, row.center().y());
        QVERIFY(hovered.toImage().pixelColor(background).lightness() >
                normal.toImage().pixelColor(background).lightness());
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, TrackListWidget::playRect(row).center());
        QCOMPARE(played.size(), 1);
        QCOMPARE(opened.size(), 0);
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, QPoint(150, row.center().y()));
        QCOMPARE(played.size(), 1);
        QCOMPARE(opened.size(), 1);
        // Artwork outside the Play button still opens details.
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, cover.topLeft() + QPoint(2, 2));
        QCOMPARE(played.size(), 1);
        QCOMPARE(opened.size(), 2);
        list.setCurrentItem(item);
        QTest::keyClick(&list, Qt::Key_Return);
        QCOMPARE(opened.size(), 3);
        QTest::keyClick(&list, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(played.size(), 2);
    }
    void androidSuggestionResponseKeepsQueriesAndTypedRecommendations() {
        QFile fixture(QFINDTESTDATA("fixtures/search-suggestions.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto response = QJsonDocument::fromJson(fixture.readAll()).object();
        const auto parsed = MusicService::parseSuggestions(response);
        QCOMPARE(parsed["queries"].toArray(), QJsonArray({"test song", "test artist"}));
        const auto items = parsed["items"].toArray();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].toObject()["kind"].toString(), "song");
        QCOMPARE(Track::fromWire(items[0].toObject()).artist, "Test artist");
        QCOMPARE(Track::fromWire(items[0].toObject()).albumId, "MPRE-album");
        QCOMPARE(items[1].toObject()["kind"].toString(), "album");
        QCOMPARE(items[1].toObject()["browseId"].toString(), "MPRE-album");
        QCOMPARE(items[2].toObject()["kind"].toString(), "artist");
        QCOMPARE(items[2].toObject()["thumbnail"].toString(), "https://example.com/artist.jpg");
        QVERIFY(MusicService::parseSuggestions({})["items"].toArray().isEmpty());
        QTemporaryDir dir;
        Library library(dir.path());
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        for (auto button : window.findChildren<NavButton *>())
            if (button->text() == "Search")
                button->click();
        music.suggestionsReady(parsed);
        auto queries = window.findChild<QListWidget *>("searchSuggestions");
        auto results = window.findChild<TrackListWidget *>("searchResults");
        QCOMPARE(queries->count(), 2);
        QCOMPARE(results->count(), 3);
        QCOMPARE(results->item(1)->data(Qt::UserRole + 3).toJsonObject()["kind"].toString(), "album");
        QSignalSpy started(&music, &MusicService::searchStarted);
        QSignalSpy played(&music, &MusicService::playbackChanged);
        QTest::qWait(30);
        QTest::mouseClick(queries->viewport(), Qt::LeftButton, {},
                          queries->visualItemRect(queries->item(0)).center());
        QCOMPARE(started.size(), 1);
        QCOMPARE(started.first().first().toString(), "test song");
        QCOMPARE(played.size(), 0);
        music.cancelSearch();
        music.suggestionsReady(parsed);
        QTest::qWait(30);
        const auto albumRow = results->visualItemRect(results->item(1));
        QTest::mouseClick(results->viewport(), Qt::LeftButton, {}, QPoint(180, albumRow.center().y()));
        QCOMPARE(window.findChild<QLabel *>("collectionTitle")->text(), "Test album");
        QCOMPARE(played.size(), 0);
    }
    void completeSearchUiSupportsSectionsPaginationPreferencesAndLocalFiles() {
        QTemporaryDir dir;
        Library library(dir.path());
        Track song{"one", "First song", "Bassem", {}, 180, "First album", "MPRE-first"};
        Track second{"two", "Second song", "Other", {}, 180};
        library.importTracks({song, second});
        QFile media(dir.filePath("local.audio"));
        QVERIFY(media.open(QIODevice::WriteOnly));
        media.write("local");
        media.close();
        library.registerMedia(song, media.fileName());
        ConnectClient remote(&library);
        MusicService music(&library, &remote);
        Discovery discovery;
        MainWindow window(&library, &music, &remote, &discovery, true);
        window.show();
        QTest::qWait(30);
        for (auto button : window.findChildren<NavButton *>())
            if (button->text() == "Search")
                button->click();
        auto input = window.findChild<QLineEdit *>("searchInput");
        window.resize(1000, 680);
        QTest::qWait(30);
        auto pages = window.findChild<QStackedWidget *>("pages");
        auto outer = qobject_cast<QScrollArea *>(pages->parentWidget()->parentWidget());
        QVERIFY(outer);
        QCOMPARE(outer->verticalScrollBar()->maximum(), 0);
        auto scope = window.findChild<QComboBox *>("searchScope");
        auto filter = window.findChild<QComboBox *>("searchFilter");
        auto list = window.findChild<TrackListWidget *>("searchResults");
        input->setText("Bassem");
        scope->setCurrentIndex(1);
        QCOMPARE(list->count(), 6); // three section headings and three library results
        QVERIFY(!music.localSearchItems(true).isEmpty());
        for (auto value : music.localSearchItems(true))
            if (value.toObject()["kind"] == "song")
                QCOMPARE(value.toObject()["id"].toString(), "one");
        scope->setCurrentIndex(0);
        music.cancelSearch();
        auto track = song.wire();
        track["kind"] = "song";
        auto album = song.wire();
        album["id"] = "MPRE-first";
        album["kind"] = "album";
        album["browseId"] = "MPRE-first";
        QJsonArray groups{QJsonObject{{"title", "Songs"}, {"items", QJsonArray{track}}},
                          QJsonObject{{"title", "Albums"}, {"items", QJsonArray{album}}}};
        music.searchSystem()->pageReady(
            {{"query", "Bassem"}, {"filter", "all"}, {"groups", groups}, {"items", QJsonArray{}}}, false);
        QCOMPARE(list->count(), 4);
        QVERIFY(list->item(0)->data(Qt::UserRole + 8).toBool());
        QVERIFY(!(list->item(0)->flags() & Qt::ItemIsSelectable));
        filter->setCurrentIndex(1);
        music.cancelSearch();
        music.searchSystem()->pageReady({{"query", "Bassem"},
                                         {"filter", "songs"},
                                         {"items", QJsonArray{track}},
                                         {"continuation", "more"}},
                                        false);
        auto original = list->item(0);
        auto next = second.wire();
        next["kind"] = "song";
        music.searchSystem()->pageReady({{"query", "Bassem"},
                                         {"filter", "songs"},
                                         {"items", QJsonArray{track, next}},
                                         {"continuation", ""}},
                                        true);
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->item(0), original);
        QVERIFY(!window.findChild<QPushButton *>("searchMore")->isVisible());
        window.findChild<QPushButton *>("searchPreferences")->click();
        auto language = window.findChild<QLineEdit *>("searchLanguage");
        auto country = window.findChild<QLineEdit *>("searchCountry");
        QVERIFY(language);
        language->setText("ar");
        country->setText("EG");
        auto buttons = language->window()->findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        buttons->button(QDialogButtonBox::Save)->click();
        QCOMPARE(music.searchSystem()->settings()["language"].toString(), "ar");
        QCOMPARE(music.searchSystem()->settings()["country"].toString(), "EG");
        music.cancelSearch();
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
        QCOMPARE(tile->text(), "Liked Songs");
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
