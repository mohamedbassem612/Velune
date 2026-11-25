#include "services/MusicService.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace velune;
class ResumeEngine : public IPlaybackEngine {
  public:
    QUrl url;
    PlaybackRequest request = 0;
    qint64 pos = 0, seekPosition = -1;
    int loads = 0, plays = 0;
    float level = .75f;
    float speed = 1;
    bool available() const override {
        return true;
    }
    void load(const QUrl &u, PlaybackRequest id) override {
        url = u;
        request = id;
        ++loads;
    }
    void play() override {
        ++plays;
    }
    void pause() override {
        emit stateChanged(request, PlaybackState::Paused);
    }
    void release() override {
        pause();
    }
    void resume() override {
        play();
    }
    void stop() override {
        pause();
    }
    void cancel() override {
        url = QUrl();
        request = 0;
        pos = 0;
    }
    void seek(qint64 v) override {
        pos = seekPosition = v;
    }
    void setVolume(float v) override {
        level = v;
    }
    void setSpeed(float v) override {
        speed = v;
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
        return PlaybackState::Paused;
    }
    void progress(qint64 v) {
        pos = v;
        emit positionChanged(request, v);
    }
};
class PlaybackResumeTests : public QObject {
    Q_OBJECT
  private slots:
    void resumesWithoutAutoplay() {
        QTemporaryDir data;
        Library library(data.path());
        QFile audio(data.filePath("song.mp3"));
        QVERIFY(audio.open(QIODevice::WriteOnly));
        audio.write("fixture");
        audio.close();
        Track first;
        first.id = "abcdefghijk";
        first.title = "First";
        first.duration = 180;
        Track last = first;
        last.id = "12345678901";
        last.title = "Last song";
        library.registerMedia(last, audio.fileName(), false);
        {
            ConnectClient client(&library);
            auto engine = std::make_unique<ResumeEngine>();
            auto *fake = engine.get();
            MusicService music(&library, &client, std::move(engine));
            music.play({first, last}, 1);
            QCOMPARE(fake->loads, 1);
            fake->progress(73456);
            music.action("pause");
            music.command({{"name", "shuffle"}, {"enabled", true}});
            music.command({{"name", "repeat"}, {"mode", 2}});
            // The periodic checkpoint also protects against an unclean exit.
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(data.filePath("last-playback.json")), 4000);
            QFile saved(data.filePath("last-playback.json"));
            QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object()["position"].toDouble(), 73456.0);
            fake->progress(81000); // Final destructor checkpoint must include the newest position.
        }
        {
            ConnectClient client(&library);
            auto engine = std::make_unique<ResumeEngine>();
            auto *fake = engine.get();
            MusicService music(&library, &client, std::move(engine));
            const auto state = music.playback();
            QCOMPARE(state["song"].toObject()["id"].toString(), last.id);
            QCOMPARE(state["index"].toInt(), 1);
            QCOMPARE(state["queue"].toArray().size(), 2);
            QCOMPARE(state["position"].toDouble(), 81000.0);
            QVERIFY(!state["playing"].toBool());
            QVERIFY(state["shuffle"].toBool());
            QCOMPARE(state["repeat"].toInt(), 2);
            QCOMPARE(fake->loads, 0);
            QCOMPARE(fake->plays, 0);
            music.seek(95000); // Seeking before the first Play must also be retained.
            music.action("resume");
            QCOMPARE(fake->loads, 1);
            QCOMPARE(fake->plays, 1);
            QCOMPARE(fake->seekPosition, 95000);
            QCOMPARE(fake->source(), QUrl::fromLocalFile(audio.fileName()));
            music.command({{"name", "clear"}});
        }
        {
            ConnectClient client(&library);
            MusicService music(&library, &client, std::make_unique<ResumeEngine>());
            QVERIFY(music.playback()["song"].toObject().isEmpty());
        }
    }
    void preferencesPersist() {
        QTemporaryDir data;
        Library library(data.path());
        {
            LibraryCatalog catalog(&library, {});
            catalog.setPreferences(
                {{"crossfadeSeconds", 4}, {"defaultPage", "Downloads"}, {"systemFont", true}});
        }
        {
            LibraryCatalog catalog(&library, {});
            QCOMPARE(catalog.preferences()["crossfadeSeconds"].toInt(), 4);
            QCOMPARE(catalog.preferences()["defaultPage"].toString(), "Downloads");
            QVERIFY(catalog.preferences()["systemFont"].toBool());
        }
    }
    void listeningPrivacyAndHistoryDeletion() {
        QTemporaryDir data;
        Library library(data.path());
        LibraryCatalog catalog(&library, {});
        Track track;
        track.id = "abcdefghijk";
        track.title = "Privacy fixture";
        library.recordListening(track, 1000, "2026-11-24T12:00:00.000");
        const auto oldSnapshot = library.snapshot();
        catalog.setPreferences({{"pauseListeningHistory", true}});
        QJsonObject state{{"song", track.wire()}, {"playing", true}};
        catalog.observePlayback(state, true);
        QTest::qWait(30);
        catalog.observePlayback(state, true);
        state["playing"] = false;
        catalog.observePlayback(state, true);
        QCOMPARE(catalog.statistics()["listeningMs"].toDouble(), 1000.0);
        catalog.clearListeningHistory();
        QCOMPARE(catalog.statistics()["events"].toInt(), 0);
        library.merge(oldSnapshot);
        QCOMPARE(catalog.statistics()["events"].toInt(), 0);
        catalog.setPreferences({{"pauseListeningHistory", false}});
        state["playing"] = true;
        catalog.observePlayback(state, true);
        QTest::qWait(30);
        state["playing"] = false;
        catalog.observePlayback(state, true);
        QVERIFY(catalog.statistics()["listeningMs"].toDouble() > 0);
    }
    void ignoresInvalidCheckpoint() {
        QTemporaryDir data;
        Library library(data.path());
        QFile file(data.filePath("last-playback.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"schema\":1,\"index\":42,\"queue\":[{\"id\":\"abcdefghijk\"}]}");
        file.close();
        ConnectClient client(&library);
        auto engine = std::make_unique<ResumeEngine>();
        auto *fake = engine.get();
        MusicService music(&library, &client, std::move(engine));
        QVERIFY(music.playback().isEmpty());
        QCOMPARE(fake->loads, 0);
    }
};
QTEST_GUILESS_MAIN(PlaybackResumeTests)
#include "PlaybackResumeTests.moc"
