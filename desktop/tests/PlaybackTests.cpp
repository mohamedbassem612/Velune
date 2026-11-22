#include "FakePlaybackEngine.h"
#include "playback/QtPlaybackEngine.h"
#include "services/MusicService.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QPluginLoader>
#include <QDir>
#include <QtTest>
#include <thread>
using namespace velune;
class PlaybackTests : public QObject {
    Q_OBJECT
    struct Session {
        QTemporaryDir directory;
        Library library{directory.path()};
        ConnectClient remote{&library};
        FakePlaybackEngine *engine = new FakePlaybackEngine;
        MusicService music{&library, &remote, std::unique_ptr<IPlaybackEngine>(engine)};
        Track first{"local:first", "First", "Test", {}, 20};
        Track second{"local:second", "Second", "Test", {}, 20};
        Session() {
            QFile file(directory.filePath("audio.wav"));
            if (!file.open(QIODevice::WriteOnly))
                qFatal("Cannot create fixture");
            file.write("fixture");
            file.close();
            library.registerMedia(first, file.fileName());
            library.registerMedia(second, file.fileName());
        }
        bool playing() const {
            return music.playback()["playing"].toBool();
        }
        QString song() const {
            return music.playback()["song"].toObject()["id"].toString();
        }
    };
  private slots:
    void rapidTenTrackControlsAndShuffle() {
        Session s;
        QList<Track> tracks;
        for (int i = 0; i < 10; ++i) {
            auto track = s.first; track.id = "local:stress" + QString::number(i);
            s.library.registerMedia(track, s.library.mediaPath(s.first.id));
            tracks.append(track);
        }
        s.music.play(tracks);
        for (int i = 1; i < 10; ++i) {
            const auto previous = s.engine->request;
            s.music.action("next");
            emit s.engine->endOfMedia(previous);
            emit s.engine->stateChanged(previous, PlaybackState::Playing);
            QCOMPARE(s.song(), tracks[i].id);
        }
        for (int i = 0; i < 100; ++i) { s.music.action("pause"); s.music.action("resume"); }
        s.music.action("pause"); QVERIFY(!s.playing());
        s.music.shuffle(true);
        for (int i = 0; i < 20; ++i) {
            s.music.action("next");
            QVERIFY(s.music.playback()["index"].toInt() >= 0);
            QVERIFY(s.music.playback()["index"].toInt() < tracks.size());
            QVERIFY(s.playing());
        }
    }
    void controlsAndPausedSeek() {
        Session s;
        s.music.play({s.first});
        QVERIFY(s.playing());
        QCOMPARE(s.engine->source(), QUrl::fromLocalFile(s.library.mediaPath(s.first.id)));
        s.music.action("pause");
        s.music.action("pause");
        QVERIFY(!s.playing());
        s.music.seek(14000);
        QCOMPARE(s.engine->position(), 14000);
        QVERIFY(!s.playing());
        s.music.action("resume");
        s.music.action("resume");
        QVERIFY(s.playing());
        s.music.seek(7000);
        QCOMPARE(s.music.playback()["position"].toDouble(), 7000.);
        s.music.volume(-10);
        QCOMPARE(s.engine->volume(), 0.f);
        s.music.volume(170);
        QCOMPARE(s.engine->volume(), 1.f);
        s.music.volume(35);
        QCOMPARE(s.engine->volume(), .35f);
        const auto old = s.engine->request;
        s.music.action("stop");
        QVERIFY(!s.playing());
        QVERIFY(s.engine->source().isEmpty());
        emit s.engine->stateChanged(old, PlaybackState::Playing);
        QVERIFY(!s.playing());
        s.music.action("resume");
        QVERIFY(s.playing());
    }
    void replacementRejectsEveryStaleEvent() {
        Session s;
        s.music.play({s.first, s.second});
        const auto old = s.engine->request;
        s.music.action("next");
        QCOMPARE(s.song(), s.second.id);
        QSignalSpy messages(&s.music, &MusicService::message);
        const auto before = s.music.playback();
        emit s.engine->positionChanged(old, 999);
        emit s.engine->durationChanged(old, 999);
        emit s.engine->stateChanged(old, PlaybackState::Paused);
        emit s.engine->errorOccurred(old, "old error");
        emit s.engine->endOfMedia(old);
        QCOMPARE(s.music.playback(), before);
        QCOMPARE(messages.size(), 0);
        s.music.action("previous");
        QCOMPARE(s.song(), s.first.id);
    }
    void queuedResolverAndRelayCallbacksCannotReplaceSelection() {
        Session s;
        s.music.play({s.first});
        auto *resolver = s.music.findChild<AudioResolver *>();
        auto *stream = s.music.findChild<AudioStream *>();
        QVERIFY(resolver);
        QVERIFY(stream);
        // Emit on another thread so AutoConnection queues the old request's handlers.
        std::thread late([&] {
            emit resolver->ready(QUrl("https://fixture.googlevideo.com/audio"), {}, 99000);
            emit resolver->error("late resolver error");
            emit stream->requestError(1, "late stream error");
        });
        late.join();
        s.music.play({s.second});
        QSignalSpy messages(&s.music, &MusicService::message);
        QCoreApplication::processEvents();
        QCOMPARE(s.song(), s.second.id);
        QCOMPARE(s.engine->calls->loads, 2);
        QCOMPARE(messages.size(), 0);
        QVERIFY(s.playing());
    }
    void preparedStreamPreservesPauseAndSeek() {
        Session s;
        s.music.play({s.first});
        auto *resolver = s.music.findChild<AudioResolver *>();
        auto *stream = s.music.findChild<AudioStream *>();
        s.music.action("pause");
        s.engine->cancel(); // Simulate waiting for a prepared source.
        s.music.seek(9000);
        emit resolver->ready(QUrl("https://fixture.googlevideo.com/audio"), {}, 20000);
        QVERIFY(!s.playing());
        QCOMPARE(s.engine->position(), 9000);
        QCOMPARE(s.engine->calls->loads, 2);
        // An older relay attempt must be ignored without trying network resolution.
        emit stream->requestError(1, "old attempt");
        QCOMPARE(s.engine->calls->loads, 2);
        QVERIFY(!s.engine->source().isEmpty());
        s.music.action("resume");
        QVERIFY(s.playing());
    }
    void refreshPreservesPositionAndRejectsDuplicateRelayErrors() {
        Session s;
        s.music.play({s.first});
        auto *resolver = s.music.findChild<AudioResolver *>();
        auto *stream = s.music.findChild<AudioStream *>();
        emit resolver->ready(QUrl("https://fixture.googlevideo.com/audio"), {}, 20000);
        s.music.seek(6000);
        s.music.action("pause");
        QSignalSpy messages(&s.music, &MusicService::message);
        {
            // The test supplies completion; never launch a real resolver or contact a CDN.
            QSignalBlocker block(resolver);
            emit stream->requestError(2, "expired source");
        }
        QCOMPARE(messages.size(), 1);
        QVERIFY(s.engine->source().isEmpty());
        emit stream->requestError(2, "duplicate expired source");
        QCOMPARE(messages.size(), 1);
        emit resolver->ready(QUrl("https://fixture.googlevideo.com/refreshed"), {}, 20000);
        QCOMPARE(s.engine->position(), 6000);
        QVERIFY(!s.playing());
        QVERIFY(!s.engine->source().isEmpty());
    }
    void endOfMediaAndRepeat() {
        Session s;
        s.music.play({s.first, s.second});
        const auto first = s.engine->request;
        emit s.engine->endOfMedia(first);
        QTRY_COMPARE(s.song(), s.second.id);
        emit s.engine->endOfMedia(first);
        QCOMPARE(s.song(), s.second.id);
        emit s.engine->endOfMedia(s.engine->request);
        QVERIFY(!s.playing());
        s.music.repeat(2);
        s.music.play({s.first, s.second}, 1);
        emit s.engine->endOfMedia(s.engine->request);
        QTRY_COMPARE(s.song(), s.first.id);
        QVERIFY(s.playing());
        s.music.repeat(1);
        const auto previous = s.engine->request;
        emit s.engine->endOfMedia(previous);
        QTRY_VERIFY(s.engine->request != previous);
        QCOMPARE(s.song(), s.first.id);
        QVERIFY(s.playing());
        emit s.engine->endOfMedia(previous);
        QCOMPARE(s.engine->calls->loads, 5);
    }
    void errorsCancelAndAllowRetry() {
        Session s;
        QSignalSpy messages(&s.music, &MusicService::message);
        s.music.play({s.first});
        auto id = s.engine->request;
        emit s.engine->errorOccurred(id, "missing media");
        QVERIFY(!s.playing());
        QVERIFY(s.engine->source().isEmpty());
        QCOMPARE(messages.last()[0].toString(), QString("missing media"));
        emit s.engine->stateChanged(id, PlaybackState::Playing);
        QVERIFY(!s.playing());
        s.music.action("resume");
        QVERIFY(s.playing());
        s.music.play({}, 0);
        QVERIFY(s.playing()); // Invalid queue does not replace working audio.
    }
    void selectedRemoteStopsBeforeConnectionAndDetachStaysPaused() {
        Session s;
        s.music.play({s.first});
        s.music.seek(3000);
        auto id = s.engine->request;
        s.remote.attach("127.0.0.1", 1, QString(64, 'a'));
        QVERIFY(s.remote.hasTarget());
        QVERIFY(!s.remote.connected());
        QVERIFY(s.engine->source().isEmpty());
        QVERIFY(!s.playing());
        emit s.engine->stateChanged(id, PlaybackState::Playing);
        QVERIFY(!s.playing());
        s.music.action("resume");
        QVERIFY(s.engine->source().isEmpty());
        for (int i = 0; i < 5; ++i) {
            s.remote.detach();
            QVERIFY(!s.playing());
            QCOMPARE(s.song(), s.first.id);
            s.remote.attach("127.0.0.1", 1, QString(64, 'b'));
            QVERIFY(s.engine->source().isEmpty());
        }
        s.remote.detach();
        s.music.action("resume");
        QVERIFY(s.playing());
        QCOMPARE(s.engine->calls->loads, 2);
    }
    void logoutAndShutdown() {
        auto s = std::make_unique<Session>();
        s->music.play({s->first});
        const auto id = s->engine->request;
        s->music.logout();
        QVERIFY(s->engine->source().isEmpty());
        QVERIFY(!s->playing());
        emit s->engine->stateChanged(id, PlaybackState::Playing);
        QVERIFY(!s->playing());
        s->music.play({s->second});
        auto calls = s->engine->calls;
        const auto cancellations = calls->cancels;
        s.reset();
        QVERIFY(calls->destroyed);
        QVERIFY(calls->cancels > cancellations);
    }
    void unavailableBackendDoesNotResolve() {
        Session s;
        s.engine->usable = false;
        QSignalSpy messages(&s.music, &MusicService::message);
        s.music.play({Track{"NOT_A_VIDEO", "Unavailable", {}, {}, 0}});
        QVERIFY(!s.playing());
        QCOMPARE(messages.size(), 1);
        QVERIFY(messages[0][0].toString().contains("Qt Multimedia"));
    }
    void reentrantSelectionDoesNotLoadOldTrack() {
        Session s;
        bool switched = false;
        connect(&s.music, &MusicService::playbackChanged, &s.music, [&] {
            if (!switched) {
                switched = true;
                s.music.play({s.second});
            }
        });
        s.music.play({s.first});
        QCOMPARE(s.song(), s.second.id);
        QCOMPARE(s.engine->calls->loads, 1);
    }
    void missingMultimediaPlugin_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("missing-explicit") << "missing-explicit";
        QTest::newRow("missing-default") << "missing-default";
        QTest::newRow("unknown-backend") << "unknown-backend";
#ifdef Q_OS_LINUX
        QTest::newRow("missing-dependency") << "missing-dependency";
#endif
    }
    void missingMultimediaPlugin() {
        QFETCH(QString, mode);
        // Each case needs a fresh process: Qt caches the multimedia integration.
        if (qEnvironmentVariable("VELUNE_QT_PLUGIN_TEST") != mode) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("VELUNE_QT_PLUGIN_TEST", mode);
            child.setProcessEnvironment(environment);
            child.start(QCoreApplication::applicationFilePath(),
                        {"-platform", "offscreen", "missingMultimediaPlugin:" + mode});
            QVERIFY(child.waitForFinished(30000));
            const auto output = child.readAllStandardOutput() + child.readAllStandardError();
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
            return;
        }
        QTemporaryDir plugins;
        QVERIFY(plugins.isValid());
        if (mode == "missing-dependency") {
#ifdef VELUNE_MULTIMEDIA
            QString original;
            for (const auto &path : QCoreApplication::libraryPaths()) {
                QDir directory(QDir(path).filePath("multimedia"));
                for (const auto &file : directory.entryList(QDir::Files)) {
                    const auto candidate = directory.absoluteFilePath(file);
                    if (QPluginLoader(candidate).metaData().value("IID").toString() ==
                        "org.qt-project.Qt.QPlatformMediaPlugin") {
                        original = candidate;
                        break;
                    }
                }
                if (!original.isEmpty()) break;
            }
            if (original.isEmpty()) QSKIP("No dynamic multimedia plugin to corrupt");
            QFile source(original);
            QVERIFY(source.open(QIODevice::ReadOnly));
            auto bytes = source.readAll();
            QVERIFY(bytes.contains("libQt6Core.so.6"));
            bytes.replace("libQt6Core.so.6", "libNo6Core.so.6");
            QVERIFY(QDir(plugins.path()).mkdir("multimedia"));
            QFile broken(plugins.filePath("multimedia/" + QFileInfo(original).fileName()));
            QVERIFY(broken.open(QIODevice::WriteOnly));
            QCOMPARE(broken.write(bytes), bytes.size());
            broken.close();
            QVERIFY(!QPluginLoader(broken.fileName()).metaData().isEmpty());
            qunsetenv("QT_MEDIA_BACKEND");
#else
            QSKIP("Qt Multimedia disabled");
#endif
        }
        if (mode != "unknown-backend") QCoreApplication::setLibraryPaths({plugins.path()});
        if (mode == "missing-explicit") qputenv("QT_MEDIA_BACKEND", "ffmpeg");
        if (mode == "missing-default") qunsetenv("QT_MEDIA_BACKEND");
        if (mode == "unknown-backend") qputenv("QT_MEDIA_BACKEND", "velune-nonexistent");
        QtPlaybackEngine engine(QByteArray("nonexistent-device"));
        QVERIFY(!engine.available());
        QCOMPARE(engine.state(), PlaybackState::Unavailable);
        QSignalSpy errors(&engine, &IPlaybackEngine::errorOccurred);
        QSignalSpy states(&engine, &IPlaybackEngine::stateChanged);
        engine.load(QUrl("file:///missing.wav"), 41);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors[0][0].toULongLong(), 41ULL);
        QVERIFY(errors[0][1].toString().contains("Qt Multimedia"));
        QCOMPARE(states.size(), 1);
        engine.play(); engine.pause(); engine.resume(); engine.seek(500); engine.stop();
        engine.setVolume(.4f);
        QCOMPARE(engine.volume(), .4f);
        QCOMPARE(engine.position(), 0);
        QCOMPARE(engine.duration(), 0);
        QVERIFY(!engine.seekable());
        engine.cancel();
        QVERIFY(engine.source().isEmpty());
        QCOMPARE(engine.state(), PlaybackState::Unavailable);
    }
    void backendIdleVolumeAndInvalidSource() {
        QtPlaybackEngine engine;
        QCOMPARE(engine.volume(), .75f);
        engine.setVolume(2.f);
        QCOMPARE(engine.volume(), 1.f);
        engine.setVolume(-1.f);
        QCOMPARE(engine.volume(), 0.f);
        QSignalSpy error(&engine, &IPlaybackEngine::errorOccurred);
        engine.load({}, 17);
        QCOMPARE(error.size(), 1);
        QCOMPARE(error[0][0].toULongLong(), 17ULL);
        QCOMPARE(engine.state(), engine.available() ? PlaybackState::Error : PlaybackState::Unavailable);
        engine.cancel();
        QVERIFY(engine.source().isEmpty());
        QCOMPARE(engine.position(), 0);
        QCOMPARE(engine.state(), engine.available() ? PlaybackState::Idle : PlaybackState::Unavailable);
    }
};
QTEST_MAIN(PlaybackTests)
#include "PlaybackTests.moc"
