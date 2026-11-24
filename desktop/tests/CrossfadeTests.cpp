#include "playback/CrossfadePlaybackEngine.h"
#include "playback/ProcessPlaybackEngine.h"
#include "services/MusicService.h"
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QtTest>
using namespace velune;
class FadeEngine : public IPlaybackEngine {
  public:
    using IPlaybackEngine::IPlaybackEngine;
    QUrl url;
    PlaybackRequest request = 0;
    float level = 0, speed = 1;
    bool playing = false, cancelled = false;
    qint64 pos = 0;
    bool available() const override {
        return true;
    }
    void load(const QUrl &u, PlaybackRequest r) override {
        url = u;
        request = r;
        cancelled = false;
        QTimer::singleShot(0, this, [this] {
            emit mediaReady(request);
            emit durationChanged(request, 4000);
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
        pause();
        emit pauseConfirmed(request);
    }
    void resume() override {
        play();
    }
    void stop() override {
        pause();
        pos = 0;
    }
    void cancel() override {
        playing = false;
        cancelled = true;
        url = QUrl();
    }
    void seek(qint64 p) override {
        pos = p;
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
        return 4000;
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
    void progress(qint64 p) {
        pos = p;
        emit positionChanged(request, p);
    }
};
class CrossfadeTests : public QObject {
    Q_OBJECT
  private slots:
    void nativeDecoderTransition() {
        if(!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/native/lib/libmpv.so.2"))
            QSKIP("Stage the audited libmpv runtime under the build's native/lib to run the decoder test.");
        const auto old=qgetenv("VELUNE_AUDIO_OUTPUT");
        qputenv("VELUNE_AUDIO_OUTPUT","null");
        auto restore=qScopeGuard([&]{if(old.isNull())qunsetenv("VELUNE_AUDIO_OUTPUT");else qputenv("VELUNE_AUDIO_OUTPUT",old);});
        QTemporaryDir data;
        for(auto name:{QString("one"),QString("two")}) {
            QProcess generate;
            generate.start(AudioResolver::toolPath("ffmpeg"),{"-v","error","-f","lavfi","-i",
                "sine=frequency="+QString(name=="one"?"440":"880")+":duration=4",data.filePath(name+".wav")});
            QVERIFY(generate.waitForFinished(15000));QCOMPARE(generate.exitCode(),0);
        }
        CrossfadePlaybackEngine engine([]{return std::make_unique<ProcessPlaybackEngine>();});
        QSignalSpy errors(&engine,&IPlaybackEngine::errorOccurred),ready(&engine,&IPlaybackEngine::mediaReady);
        QSignalSpy nextReady(&engine,&IPlaybackEngine::nextPrepared),finished(&engine,&IPlaybackEngine::crossfadeFinished);
        engine.load(QUrl::fromLocalFile(data.filePath("one.wav")),1);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,15000);
        engine.play();QTRY_VERIFY_WITH_TIMEOUT(engine.state()==PlaybackState::Playing,15000);
        engine.prepareNext(QUrl::fromLocalFile(data.filePath("two.wav")),7);
        QTRY_COMPARE_WITH_TIMEOUT(nextReady.size(),1,15000);QVERIFY(nextReady[0][1].toBool());
        QVERIFY(engine.activateNext(2,500));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);
        QTRY_VERIFY(engine.position()>0);QCOMPARE(errors.size(),0);
        QCOMPARE(engine.source(),QUrl::fromLocalFile(data.filePath("two.wav")));
        engine.pause();
    }
    void overlapAndVolumeRamp() {
        QList<QPointer<FadeEngine>> children;
        CrossfadePlaybackEngine engine([&] {
            auto e = std::make_unique<FadeEngine>();
            children.append(e.get());
            return e;
        });
        engine.setVolume(.8);
        engine.load(QUrl("file:///one.mp3"), 10);
        engine.play();
        QSignalSpy ready(&engine, &IPlaybackEngine::nextPrepared);
        engine.prepareNext(QUrl("file:///two.mp3"), 22);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(children.size(), 2);
        QCOMPARE(children[1]->level, 0.f);
        QVERIFY(!children[1]->playing);
        auto old = children[0], next = children[1];
        QVERIFY(engine.activateNext(11, 400));
        QVERIFY(old->playing);
        QVERIFY(next->playing);
        QSignalSpy positions(&engine, &IPlaybackEngine::positionChanged);
        old->progress(3999);
        QCOMPARE(positions.size(), 0);
        next->progress(100);
        QCOMPARE(positions.size(), 1);
        QCOMPARE(positions[0][0].toULongLong(), 11ull);
        QTest::qWait(150);
        QVERIFY(next->level > 0 && next->level < .8);
        QVERIFY(old->level > 0 && old->level < .8);
        QVERIFY(qAbs(next->level + old->level - .8f) < .01f);
        engine.setVolume(.5);
        QVERIFY(qAbs(next->level + old->level - .5f) < .01f);
        QTRY_VERIFY(old.isNull());
        QCOMPARE(next->level, .5f);
        QCOMPARE(engine.source(), QUrl("file:///two.mp3"));
    }
    void pauseAndReleaseStopRetiringAudio() {
        for (bool release : {false, true}) {
            QList<QPointer<FadeEngine>> children;
            CrossfadePlaybackEngine engine([&] {
                auto e = std::make_unique<FadeEngine>();
                children.append(e.get());
                return e;
            });
            engine.load(QUrl("file:///one.mp3"), 10);
            engine.play();
            QSignalSpy ready(&engine, &IPlaybackEngine::nextPrepared);
            engine.prepareNext(QUrl("file:///two.mp3"), 22);
            QTRY_COMPARE(ready.size(), 1);
            QVERIFY(engine.activateNext(11, 1000));
            auto old = children[0], next = children[1];
            QSignalSpy acknowledgement(&engine, &IPlaybackEngine::pauseConfirmed);
            if (release)
                engine.release();
            else
                engine.pause();
            QVERIFY(old.isNull());
            QVERIFY(!next->playing);
            QCOMPARE(next->level, engine.volume());
            if (release)
                QCOMPARE(acknowledgement.size(), 1);
        }
    }
    void automaticQueueTransitionAndNormalEndFallback() {
        for (bool enable : {true, false}) {
            QTemporaryDir data;
            Library library(data.path());
            QFile f(data.filePath("local.mp3"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("fixture");
            f.close();
            Track one;
            one.id = "abcdefghijk";
            one.title = "One";
            one.duration = 4;
            Track two = one;
            two.id = "12345678901";
            two.title = "Two";
            library.registerMedia(one, f.fileName(), false);
            library.registerMedia(two, f.fileName(), false);
            ConnectClient client(&library);
            QList<QPointer<FadeEngine>> children;
            auto engine = std::make_unique<CrossfadePlaybackEngine>([&] {
                auto e = std::make_unique<FadeEngine>();
                children.append(e.get());
                return e;
            });
            MusicService music(&library, &client, std::move(engine));
            music.libraryCatalog()->setPreferences({{"crossfadeSeconds", enable ? 2 : 0}});
            music.play({one, two});
            if (enable) {
                QTRY_COMPARE(children.size(), 2);
                QTest::qWait(20);
                children[0]->progress(3500);
                QTRY_COMPARE(music.playback()["song"].toObject()["id"].toString(), two.id);
                QVERIFY(children[0]->playing);
                QVERIFY(children[1]->playing);
                QTRY_VERIFY(children[0].isNull());
            } else {
                QTest::qWait(300);
                QCOMPARE(children.size(), 1);
                children[0]->progress(3500);
                QCOMPARE(music.playback()["index"].toInt(), 0);
            }
        }
    }
};
QTEST_GUILESS_MAIN(CrossfadeTests)
#include "CrossfadeTests.moc"
