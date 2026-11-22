#include "services/AudioResolver.h"
#include "services/AudioStream.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>
#ifdef VELUNE_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoSink>
#endif
using namespace velune;
class AudioResolverTests : public QObject {
    Q_OBJECT
    QString fixture() const {
#ifdef Q_OS_WIN
        return QCoreApplication::applicationDirPath() + "/velune_resolver_fixture.exe";
#else
        return QCoreApplication::applicationDirPath() + "/velune_resolver_fixture";
#endif
    }
  private slots:
    void sourceCacheAndCookieCleanup() {
        AudioResolver resolver(nullptr, fixture(), fixture());
        QSignalSpy ready(&resolver, &AudioResolver::ready), errors(&resolver, &AudioResolver::error);
        auto temporary = [] {
            return QDir(QDir::tempPath()).entryList({"velune-playback-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        };
        const auto before = temporary();
        resolver.resolve("CACHECACHEC", "# Netscape HTTP Cookie File\n");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(ready[0][0].toUrl().host(), QString("fixture.googlevideo.com"));
        QCOMPARE(ready[0][2].toLongLong(), 180000);
        QCOMPARE(temporary(), before);
        resolver.resolve("CACHECACHEC", "# Netscape HTTP Cookie File\n");
        QCOMPARE(ready.size(), 2); // No process/network/download on a cache hit.
        resolver.cancel();
    }
    void errorsCancellationAndUnexpectedPaths() {
        AudioResolver resolver(nullptr, fixture(), fixture());
        QSignalSpy ready(&resolver, &AudioResolver::ready), errors(&resolver, &AudioResolver::error);
        resolver.resolve("invalid-id;echo secret");
        QCOMPARE(errors.size(), 1);
        resolver.resolve("FAILFAILFAI");
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 5000);
        QVERIFY(!errors.last()[0].toString().contains("secret-cookie"));
        resolver.resolve("PATHPATHPAT");
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 3, 5000);
        resolver.resolve("SLOWSLOWSLO");
        QTest::qWait(50);
        resolver.cancel();
        QTest::qWait(150);
        QCOMPARE(ready.size(), 0);
        // A new request after cancellation must still succeed.
        resolver.resolve("GOODGOODGOO");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(errors.size(), 3);
    }
    void liveYouTubeAndNativePlayback() {
        const auto id = qEnvironmentVariable("VELUNE_LIVE_PLAYBACK_ID");
        if (id.isEmpty())
            QSKIP("Set VELUNE_LIVE_PLAYBACK_ID to explicitly run the live YouTube playback check.");
#ifdef VELUNE_MULTIMEDIA
        AudioResolver resolver;
        QSignalSpy ready(&resolver, &AudioResolver::ready), errors(&resolver, &AudioResolver::error);
        QElapsedTimer clock;
        clock.start();
        resolver.resolve(id);
        QTRY_VERIFY_WITH_TIMEOUT(ready.size() || errors.size(), 125000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors[0][0].toString()));
        QCOMPARE(ready.size(), 1);
        qInfo() << "Stream resolution ms:" << clock.elapsed();
        AudioStream stream;
        const auto local = stream.start(ready[0][0].toUrl(), ready[0][1].toJsonObject());
        QVERIFY(!local.isEmpty());
        QMediaPlayer player;
        QAudioOutput output;
        output.setMuted(true);
        player.setAudioOutput(&output);
        player.setVideoSink(new QVideoSink(&player));
        connect(&player, &QMediaPlayer::tracksChanged, &player, [&] {
            QTimer::singleShot(0, &player, [&] {
                if (player.activeVideoTrack() != -1)
                    player.setActiveVideoTrack(-1);
            });
        });
        player.setSource(local);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 500 || player.error() != QMediaPlayer::NoError, 10000);
        QCOMPARE(player.error(), QMediaPlayer::NoError);
        QVERIFY(player.position() > 500);
        qInfo() << "First playback ms:" << clock.elapsed();
        QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 2000, 10000);
        const auto target = player.duration() / 2;
        player.setPosition(target);
        QTest::qWait(1200);
        QVERIFY2(player.position() >= target - 300 && player.position() < target + 4000,
                 "Seek must remain applied while audio continues playing.");
        const auto earlier = player.duration() / 4;
        player.setPosition(earlier);
        QTest::qWait(1000);
        QVERIFY(player.position() >= earlier - 300 && player.position() < earlier + 4000);
        player.pause();
        QCOMPARE(player.playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(player.error(), QMediaPlayer::NoError);
        player.stop();
        player.setSource({});
        stream.stop();
        resolver.cancel();
#else
        QSKIP("Native audio requires Qt Multimedia.");
#endif
    }
};
QTEST_MAIN(AudioResolverTests)
#include "AudioResolverTests.moc"
