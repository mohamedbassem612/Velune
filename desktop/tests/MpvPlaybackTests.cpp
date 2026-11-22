#include "FixtureNetwork.h"
#include "PlaybackFixture.h"
#include "playback/MpvPlaybackEngine.h"
#include "playback/PlaybackEngineFactory.h"
#include "playback/PlaybackTrace.h"
#include "services/AudioStream.h"
#include "services/MusicService.h"
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
#ifdef VELUNE_MPV
#include "FakeMpvApi.h"
#include "playback/MpvApi.h"
#endif
using namespace velune;
class MpvPlaybackTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString wav;
    std::unique_ptr<MpvPlaybackEngine> native() {
#ifdef VELUNE_MPV
        QString error;
        auto api = MpvApi::open(error);
        if (api)
            return std::make_unique<MpvPlaybackEngine>(api, "null");
#endif
        return {};
    }
  private slots:
    void initTestCase() {
        wav = directory.filePath("tone.wav");
        QVERIFY(writePlaybackWav(wav));
    }
    void selectionAndTrace() {
        auto qt = selectPlaybackEngine("qt");
        QCOMPARE(qt.engine->backendName(), QString("qt"));
        auto invalid = selectPlaybackEngine("invalid-secret-must-not-appear");
        QVERIFY(!invalid.diagnostic.isEmpty());
        QVERIFY(!invalid.diagnostic.contains("invalid-secret"));
        auto mpv = selectPlaybackEngine("mpv");
        QVERIFY(mpv.engine);
        if (!mpv.diagnostic.isEmpty())
            QVERIFY(mpv.engine->backendName() == "qt" || !mpv.engine->available());
        PlaybackTrace disabled(false);
        disabled.begin(1, "qt", "local");
        disabled.mark(1, "backend_load");
        QVERIFY(!disabled.snapshot().contains("request"));
        PlaybackTrace trace(true);
        trace.begin(42, "mpv", "local");
        trace.mark(41, "stale");
        trace.mark(42, "backend_load");
        const auto sample = trace.snapshot();
        QCOMPARE(sample["request"].toString(), QString("42"));
        QVERIFY(sample["first_audio_output_ms"].isNull());
        QVERIFY(!sample["stages_ms"].toObject().contains("stale"));
        QVERIFY(sample["stages_ms"].toObject()["backend_load"].toDouble() >= 0);
    }
    void initializationFailure() {
#ifdef VELUNE_MPV
        auto api = std::make_shared<MpvApi>();
        api->create = []() -> mpv_handle * { return nullptr; };
        MpvPlaybackEngine engine(api, {});
        QVERIFY(!engine.available());
        QSignalSpy errors(&engine, &IPlaybackEngine::errorOccurred);
        engine.load(QUrl::fromLocalFile(wav), 5);
        QCOMPARE(errors.size(), 1);
        errors.clear();
        bool reentered = false;
        connect(&engine, &IPlaybackEngine::stateChanged, &engine, [&] {
            if (!reentered) {
                reentered = true;
                engine.load(QUrl::fromLocalFile(wav), 5);
            }
        });
        engine.load(QUrl::fromLocalFile(wav), 5);
        QCOMPARE(errors.size(), 1);
#else
        QSKIP("libmpv headers were not compiled in this build");
#endif
    }
    void deterministicNativeEventIdentityAndOverflow() {
#ifdef VELUNE_MPV
        auto fake = std::make_shared<FakeMpv>();
        auto engine = std::make_unique<MpvPlaybackEngine>(fake->api(fake), QString());
        QSignalSpy ready(engine.get(), &IPlaybackEngine::mediaReady);
        QSignalSpy positions(engine.get(), &IPlaybackEngine::positionChanged);
        QSignalSpy buffering(engine.get(), &IPlaybackEngine::bufferingChanged);
        QSignalSpy ends(engine.get(), &IPlaybackEngine::endOfMedia);
        QSignalSpy errors(engine.get(), &IPlaybackEngine::errorOccurred);
        engine->load(QUrl::fromLocalFile(wav), 51);
        QTRY_COMPARE(ready.size(), 1);
        const auto oldEpoch = fake->observer;
        const auto oldEntry = fake->entry;
        engine->load(QUrl::fromLocalFile(wav), 52);
        QTRY_COMPARE(ready.size(), 2);
        fake->property("time-pos", 99, oldEpoch);
        fake->ended(oldEntry, MPV_END_FILE_REASON_ERROR);
        // An old command reply has no right to affect the new source.
        auto &stale = fake->push(MPV_EVENT_COMMAND_REPLY, 999999);
        stale.event.error = MPV_ERROR_LOADING_FAILED;
        fake->property("time-pos", 1.5);
        QTRY_COMPARE(positions.size(), 1);
        QCOMPARE(positions[0][0].toULongLong(), 52ULL);
        QCOMPARE(engine->position(), 1500);
        QCOMPARE(errors.size(), 0);
        fake->property("cache-buffering-state", 0, oldEpoch);
        fake->property("cache-buffering-state", 25);
        fake->property("cache-buffering-state", 100);
        QTRY_COMPARE(buffering.size(), 2);
        QCOMPARE(buffering[0][1].toFloat(), .25f);
        QCOMPARE(buffering[1][1].toFloat(), 1.f);
        fake->ended(fake->entry, MPV_END_FILE_REASON_EOF);
        fake->ended(fake->entry, MPV_END_FILE_REASON_EOF);
        QTRY_COMPARE(ends.size(), 1);
        engine->load(QUrl::fromLocalFile(wav), 53);
        QTRY_COMPARE(ready.size(), 3);
        fake->push(MPV_EVENT_QUEUE_OVERFLOW);
        fake->wake();
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(engine->state(), PlaybackState::Error);
        engine.reset();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QVERIFY(fake->destroyed);
#else
        QSKIP("libmpv headers were not compiled in this build");
#endif
    }
    void volumeAppliedBeforeFirstUnpause() {
#ifdef VELUNE_MPV
        auto fake = std::make_shared<FakeMpv>();
        MpvPlaybackEngine engine(fake->api(fake), {});
        engine.setVolume(0);
        engine.load(QUrl::fromLocalFile(wav), 70);
        engine.play();
        QTRY_VERIFY(fake->operations.contains("pause=0"));
        QVERIFY(fake->operations.indexOf("volume=0") < fake->operations.indexOf("pause=0"));
#else
        QSKIP("libmpv headers were not compiled in this build");
#endif
    }
    void cancellationBeforeNativeStartDoesNotNeedAnotherIdleChange() {
#ifdef VELUNE_MPV
        auto fake = std::make_shared<FakeMpv>();
        fake->startFiles = false;
        fake->notifyIdleOnStop = false;
        MpvPlaybackEngine engine(fake->api(fake), {});
        QSignalSpy ready(&engine, &IPlaybackEngine::mediaReady);
        engine.load(QUrl::fromLocalFile(wav), 80);
        engine.play();
        QTRY_COMPARE(fake->loads, 1);
        fake->startFiles = true;
        engine.load(QUrl::fromLocalFile(wav), 81);
        engine.play();
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready[0][0].toULongLong(), 81ULL);
#else
        QSKIP("libmpv headers were not compiled in this build");
#endif
    }
    void nativeFormats_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("wav-generated") << wav;
        const auto fixtures = qEnvironmentVariable("VELUNE_CODEC_FIXTURES");
        if (!fixtures.isEmpty()) {
            for (const auto &name :
                 {"tone.mp3", "tone.m4a", "tone.opus", "tone.flac", "tone.wav", "audio-video.mp4"})
                QTest::newRow(name) << QDir(fixtures).filePath(name);
        }
    }
    void nativeFormats() {
        QFETCH(QString, path);
        auto engine = native();
        if (!engine)
            QSKIP("libmpv unavailable");
        QVERIFY(QFileInfo::exists(path));
        QSignalSpy errors(engine.get(), &IPlaybackEngine::errorOccurred);
        engine->load(QUrl::fromLocalFile(path), 60);
        engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(engine->position() > 100, 5000);
        QVERIFY(engine->duration() > 0);
        QCOMPARE(errors.size(), 0);
    }
    void nativeLifecycle() {
        auto engine = native();
        if (!engine)
            QSKIP("libmpv runtime/development headers unavailable");
        QVERIFY2(engine->available(), qPrintable(engine->initializationError()));
        QSignalSpy errors(engine.get(), &IPlaybackEngine::errorOccurred);
        QSignalSpy ready(engine.get(), &IPlaybackEngine::mediaReady);
        engine->load(QUrl::fromLocalFile(wav), 1);
        engine->seek(500);
        engine->seek(1000); // Last deferred seek wins without starting audio.
        QTRY_COMPARE_WITH_TIMEOUT(engine->state(), PlaybackState::Paused, 5000);
        QTRY_VERIFY(engine->seekable());
        QTRY_VERIFY(engine->duration() >= 3900);
        QTRY_VERIFY(qAbs(engine->position() - 1000) < 150);
        QCOMPARE(ready.size(), 1);
        engine->play();
        QTRY_COMPARE(engine->state(), PlaybackState::Playing);
        QTRY_VERIFY(engine->position() > 1100);
        for (int i = 0; i < 100; ++i) {
            engine->pause();
            engine->resume();
        }
        engine->pause();
        QTRY_COMPARE(engine->state(), PlaybackState::Paused);
        engine->seek(2000);
        QTRY_VERIFY(qAbs(engine->position() - 2000) < 150);
        QCOMPARE(engine->state(), PlaybackState::Paused);
        engine->setVolume(2);
        QCOMPARE(engine->volume(), 1.f);
        engine->setVolume(std::numeric_limits<float>::quiet_NaN());
        QCOMPARE(engine->volume(), 1.f);
        engine->stop();
        QCOMPARE(engine->state(), PlaybackState::Stopped);
        QVERIFY(!engine->source().isEmpty());
        engine->resume();
        QTRY_COMPARE(engine->state(), PlaybackState::Playing);
        QTRY_VERIFY(engine->position() < 1000);
        engine->stop();
        engine->seek(1500);
        engine->resume();
        QTRY_VERIFY(engine->position() >= 1400 && engine->position() < 2500);
        engine->cancel();
        QVERIFY(engine->source().isEmpty());
        QCOMPARE(engine->position(), 0);
        QCOMPARE(errors.size(), 0);
    }
    void rapidSwitchEndErrorsAndReentrancy() {
        auto engine = native();
        if (!engine)
            QSKIP("libmpv unavailable");
        QSignalSpy errors(engine.get(), &IPlaybackEngine::errorOccurred);
        QSignalSpy ended(engine.get(), &IPlaybackEngine::endOfMedia);
        for (quint64 id = 1; id <= 10; ++id) {
            engine->load(QUrl::fromLocalFile(wav), id);
            engine->play();
            QCoreApplication::processEvents();
        }
        QTRY_COMPARE_WITH_TIMEOUT(engine->state(), PlaybackState::Playing, 5000);
        engine->seek(3800);
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
        QCOMPARE(ended[0][0].toULongLong(), 10ULL);
        engine->load(QUrl::fromLocalFile(wav), 11);
        engine->play();
        QTRY_COMPARE(engine->state(), PlaybackState::Playing);
        QCOMPARE(errors.size(), 0);
        engine->load(QUrl::fromLocalFile(directory.filePath("missing.wav")), 12);
        engine->play();
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors[0][0].toULongLong(), 12ULL);
        QVERIFY(!errors[0][1].toString().contains(directory.path()));
        engine->stop();
        QCOMPARE(engine->state(), PlaybackState::Stopped);
        engine->resume();
        QTRY_COMPARE(errors.size(), 2);
        bool replaced = false;
        connect(engine.get(), &IPlaybackEngine::mediaReady, engine.get(), [&](PlaybackRequest id) {
            if (id == 13) {
                replaced = true;
                engine->load(QUrl::fromLocalFile(wav), 14);
                engine->play();
            }
        });
        engine->load(QUrl::fromLocalFile(wav), 13);
        engine->play();
        QTRY_VERIFY(replaced);
        QTRY_COMPARE(engine->state(), PlaybackState::Playing);
        engine->cancel();
    }
    void relayAndDestructionWhileLoading() {
        auto engine = native();
        if (!engine)
            QSKIP("libmpv unavailable");
        FixtureNetwork network;
        QFile file(wav);
        QVERIFY(file.open(QIODevice::ReadOnly));
        network.bytes = file.readAll();
        AudioStream relay(nullptr, &network);
        auto url = relay.start(QUrl("https://fixture.googlevideo.com/audio"), {}, 31);
        QSignalSpy errors(engine.get(), &IPlaybackEngine::errorOccurred);
        engine->load(url, 31);
        engine->play();
        QTRY_COMPARE_WITH_TIMEOUT(engine->state(), PlaybackState::Playing, 5000);
        QTRY_VERIFY(engine->position() > 100);
        engine->seek(2000);
        QTRY_VERIFY(engine->position() >= 1900);
        engine->cancel();
        relay.stop();
        network.delay = 5000;
        url = relay.start(QUrl("https://fixture.googlevideo.com/audio"), {}, 32);
        engine->load(url, 32);
        engine->play();
        QTest::qWait(30);
        engine->stop();
        QCOMPARE(engine->state(), PlaybackState::Stopped);
        QVERIFY(!engine->source().isEmpty());
        engine->cancel();
        relay.stop();
        url = relay.start(QUrl("https://fixture.googlevideo.com/audio"), {}, 33);
        engine->load(url, 33);
        engine->play();
        QTest::qWait(30);
        QElapsedTimer clock;
        clock.start();
        engine.reset();
        QVERIFY2(clock.elapsed() < 250, "Destruction must not wait for the stalled source");
        relay.stop();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    }
    void nativeConnectRoutingAndLogout() {
        auto owned = native();
        if (!owned)
            QSKIP("libmpv unavailable");
        auto *engine = owned.get();
        QTemporaryDir data;
        Library library(data.path());
        ConnectClient remote(&library);
        MusicService music(&library, &remote, std::move(owned));
        Track track{"local:native-route", "Fixture", "Test", {}, 4};
        library.registerMedia(track, wav);
        music.play({track});
        QTRY_VERIFY(music.playback()["playing"].toBool());
        remote.attach("127.0.0.1", 1, QString(64, 'a'));
        QVERIFY(engine->source().isEmpty());
        QVERIFY(!music.playback()["playing"].toBool());
        QCoreApplication::processEvents();
        QVERIFY(!music.playback()["playing"].toBool());
        remote.detach();
        QVERIFY(!music.playback()["playing"].toBool());
        music.action("resume");
        QTRY_VERIFY(music.playback()["playing"].toBool());
        music.logout();
        QVERIFY(engine->source().isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(!music.playback()["playing"].toBool());
    }
};
QTEST_MAIN(MpvPlaybackTests)
#include "MpvPlaybackTests.moc"
