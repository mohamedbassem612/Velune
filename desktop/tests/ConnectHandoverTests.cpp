#include "services/MusicService.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace velune;

// Real TLS/framing/session services, isolated data, and a controllable audio acknowledgement.
class HandoverEngine : public IPlaybackEngine {
  public:
    QUrl url;
    PlaybackRequest request = 0;
    qint64 pos = 0;
    bool playing = false, failLoad = false, acknowledge = true;
    int releases = 0;
    float level = .75f, rate = 1;
    bool available() const override {
        return true;
    }
    void load(const QUrl &u, PlaybackRequest id) override {
        url = u;
        request = id;
        playing = false;
        QTimer::singleShot(0, this, [this, id] {
            if (id != request)
                return;
            if (failLoad)
                emit errorOccurred(id, "Fixture preparation failure");
            else
                emit mediaReady(id);
        });
    }
    void play() override {
        playing = true;
        emit stateChanged(request, PlaybackState::Playing);
    }
    void pause() override {
        playing = false;
        emit stateChanged(request, PlaybackState::Paused);
    }
    void release() override {
        ++releases;
        pause();
        if (acknowledge)
            confirm();
    }
    void confirm() {
        emit pauseConfirmed(request);
    }
    void resume() override {
        play();
    }
    void stop() override {
        pause();
    }
    void cancel() override {
        playing = false;
        url = QUrl();
        request = 0;
    }
    void seek(qint64 p) override {
        pos = p;
        emit positionChanged(request, p);
    }
    void setVolume(float v) override {
        level = v;
    }
    void setSpeed(float v) override {
        rate = v;
    }
    float volume() const override {
        return level;
    }
    qint64 position() const override {
        return pos;
    }
    qint64 duration() const override {
        return 180000;
    }
    bool seekable() const override {
        return true;
    }
    QUrl source() const override {
        return url;
    }
    PlaybackState state() const override {
        return playing ? PlaybackState::Playing : PlaybackState::Paused;
    }
};
struct Device {
    QTemporaryDir data;
    Library library{data.path()};
    ConnectClient client{&library};
    HandoverEngine *audio = new HandoverEngine;
    MusicService music{&library, &client, std::unique_ptr<IPlaybackEngine>(audio)};
    Device() {
        // Keep authentication/keyring restoration outside this isolated protocol test.
        QCoreApplication::removePostedEvents(music.findChild<EmbeddedSession *>(), QEvent::MetaCall);
    }
    void cache(const QList<Track> &tracks) {
        for (const auto &track : tracks) {
            QFile file(data.filePath(track.id + ".mp3"));
            if (!file.open(QIODevice::WriteOnly))
                qFatal("Cannot create fixture");
            file.write("Audio is supplied by the fake engine");
            file.close();
            library.registerMedia(track, file.fileName(), false);
        }
    }
};
class ConnectHandoverTests : public QObject {
    Q_OBJECT
    QList<Track> tracks() {
        QList<Track> result;
        for (auto id : {"abcdefghijk", "12345678901", "abcdefghij2"}) {
            Track t;
            t.id = id;
            t.title = id;
            t.duration = 180;
            result.append(t);
        }
        return result;
    }
  private slots:
    void roundTripAndAndroidCommands() {
        Device host, source;
        auto songs = tracks();
        host.cache(songs);
        source.cache(songs);
        source.music.play(songs, 1);
        source.music.seek(42000);
        source.music.command({{"name", "speed"}, {"speed", 1.25}});
        source.music.command({{"name", "repeat"}, {"mode", 2}});
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_VERIFY_WITH_TIMEOUT(host.client.rendersLocally() && host.audio->playing, 6000);
        QVERIFY(!source.audio->playing);
        QTRY_COMPARE(source.client.playback()["rendererId"].toString(), host.client.identity());
        QCOMPARE(host.audio->pos, 42000);
        QCOMPARE(host.audio->rate, 1.25f);
        QCOMPARE(host.music.playback()["repeat"].toInt(), 2);
        QCOMPARE(host.music.playback()["index"].toInt(), 1);
        source.client.command({{"name", "shuffle"}, {"enabled", true}});
        const QJsonArray order{1, 0, 2};
        source.client.command({{"name", "order"}, {"order", order}}); // Android wire name.
        QTRY_COMPARE(host.music.playback()["order"].toArray(), order);
        source.client.command(
            {{"name", "sleepTimer"},
             {"timer", QJsonObject{{"id", "timer"}, {"remainingMs", -1}, {"endOfSong", true}}}});
        QTRY_COMPARE(host.music.playback()["sleepTimer"].toObject()["id"].toString(), QString("timer"));
        source.client.chooseThisComputer();
        QTRY_VERIFY_WITH_TIMEOUT(source.client.rendersLocally() && source.audio->playing, 6000);
        QVERIFY(!host.audio->playing);
        QCOMPARE(source.audio->pos, 42000);
        QCOMPARE(source.audio->rate, 1.25f);
        QCOMPARE(source.music.playback()["order"].toArray(), order);
        QVERIFY(source.music.playback()["sleepTimer"].toObject()["endOfSong"].toBool());
        source.music.action("next");
        QTRY_COMPARE(source.music.playback()["index"].toInt(), 0);
    }
    void waitsForReleaseAndIgnoresStaleAcknowledgement() {
        Device host, source;
        auto songs = tracks();
        host.cache(songs);
        source.cache(songs);
        source.audio->acknowledge = false;
        source.music.play(songs);
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_COMPARE_WITH_TIMEOUT(source.audio->releases, 1, 6000);
        QVERIFY(!host.audio->playing);
        QVERIFY(source.client.rendersLocally());
        emit source.audio->pauseConfirmed(source.audio->request + 100);
        QTest::qWait(50);
        QVERIFY(!host.audio->playing);
        source.audio->confirm();
        QTRY_VERIFY(host.audio->playing);
        QVERIFY(!source.audio->playing);
    }
    void transfersPeerAudioBeforeReleasingSource() {
        Device host, source;
        auto songs = tracks();
        source.cache(songs);
        source.music.play(songs);
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_VERIFY_WITH_TIMEOUT(host.client.rendersLocally() && host.audio->playing, 6000);
        QVERIFY(!source.audio->playing);
        QFile received(host.audio->source().toLocalFile());
        QVERIFY(received.open(QIODevice::ReadOnly));
        QCOMPARE(received.readAll(), QByteArray("Audio is supplied by the fake engine"));
        source.client.detach();
        QTRY_VERIFY(host.client.peerInfo().isEmpty());
        QVERIFY(host.audio->playing);
        QVERIFY(QFileInfo::exists(host.audio->source().toLocalFile()));
    }
    void releaseTimeoutStaysPausedAndCanRetry() {
        Device host, source;
        auto songs = tracks();
        host.cache(songs);
        source.cache(songs);
        source.audio->acknowledge = false;
        source.music.play(songs);
        QSignalSpy errors(&host.client, &ConnectClient::error);
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_COMPARE_WITH_TIMEOUT(source.audio->releases, 1, 6000);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 10000);
        QVERIFY(!host.audio->playing);
        QVERIFY(!source.audio->playing);
        source.audio->confirm(); // Late acknowledgement cannot commit an aborted transfer.
        QTest::qWait(50);
        QVERIFY(!host.client.rendersLocally());
        source.audio->acknowledge = true;
        source.music.action("resume");
        QTRY_VERIFY(source.audio->playing);
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_VERIFY_WITH_TIMEOUT(host.audio->playing, 6000);
        QVERIFY(!source.audio->playing);
    }
    void pausedTransferAndControllerDisconnect() {
        Device host, source;
        auto songs = tracks();
        host.cache(songs);
        source.cache(songs);
        source.music.play(songs);
        source.music.seek(23000);
        source.music.action("pause");
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_VERIFY_WITH_TIMEOUT(host.client.rendersLocally(), 6000);
        QVERIFY(!host.audio->playing);
        QVERIFY(!source.audio->playing);
        QCOMPARE(host.audio->pos, 23000);
        QTRY_COMPARE(source.client.playback()["rendererId"].toString(), host.client.identity());
        source.music.action("resume");
        QTRY_VERIFY(host.audio->playing);
        source.client.detach();
        QTRY_VERIFY(host.client.peerInfo().isEmpty());
        QVERIFY(host.audio->playing);
        QVERIFY(!source.audio->playing);
        QCOMPARE(host.music.playback()["queue"].toArray().size(), 3);
    }
    void preparationFailureKeepsSourceAndCanRetry() {
        Device host, source;
        auto songs = tracks();
        host.cache(songs);
        source.cache(songs);
        host.audio->failLoad = true;
        QSignalSpy errors(&host.client, &ConnectClient::error);
        source.music.play(songs);
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 6000);
        QVERIFY(source.audio->playing);
        QVERIFY(!host.audio->playing);
        QCOMPARE(source.audio->releases, 0);
        host.audio->failLoad = false;
        source.client.attach("127.0.0.1", host.client.listeningPort(), host.client.identity());
        QTRY_VERIFY_WITH_TIMEOUT(host.audio->playing, 6000);
        QVERIFY(!source.audio->playing);
    }
};
QTEST_GUILESS_MAIN(ConnectHandoverTests)
#include "ConnectHandoverTests.moc"
