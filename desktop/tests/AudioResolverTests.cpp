#include "NativeResolverFixture.h"
#include "playback/PlaybackEngineFactory.h"
#include "services/AudioResolver.h"
#include "services/AudioStream.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>
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
    void nativeRejectsShortPreviews() {
        NativeFixtureNetwork network;
        network.duration = 30000;
        NativeAudioResolver resolver(nullptr, &network);
        QSignalSpy ready(&resolver, &NativeAudioResolver::ready),
            errors(&resolver, &NativeAudioResolver::unavailable);
        resolver.resolve("PREVIEWTEST");
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(network.probes, 0);
    }
    void nativeColdVisitorContextIsReused() {
        NativeFixtureNetwork network;
        network.requireVisitor = true;
        NativeAudioResolver resolver(nullptr, &network);
        QSignalSpy ready(&resolver, &NativeAudioResolver::ready);
        resolver.resolve("VISITORTEST");
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(network.playerBodies.size(), 2);
        QCOMPARE(
            network.playerBodies.last()["context"].toObject()["client"].toObject()["visitorData"].toString(),
            QString("fixture-visitor"));
        QVERIFY(!network.requests.last().hasRawHeader("X-Goog-Visitor-Id"));
    }
    void nativeSourceAndSessionScopedCache() {
        NativeFixtureNetwork network;
        AudioResolver resolver(nullptr, fixture(), fixture(), &network);
        QSignalSpy ready(&resolver, &AudioResolver::ready), errors(&resolver, &AudioResolver::error);
        QSignalSpy hits(&resolver, &AudioResolver::cacheHit), stages(&resolver, &AudioResolver::stage);
        resolver.resolve("NATIVECACHE", "account-a");
        QVERIFY(resolver.matches("NATIVECACHE", "account-a"));
        QVERIFY(!resolver.matches("NATIVECACHE", "account-b"));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(network.playerBodies.size(), 1);
        QCOMPARE(network.probes, 1);
        QCOMPARE(network.requests.last().rawHeader("Range"), QByteArray("bytes=0-0"));
        for (const auto &request : network.requests) {
            QVERIFY(!request.hasRawHeader("Cookie"));
            QVERIFY(!request.hasRawHeader("Authorization"));
        }
        resolver.resolve("NATIVECACHE", "account-a");
        QCOMPARE(ready.size(), 2);
        QCOMPARE(hits.size(), 1);
        resolver.resolve("NATIVECACHE", "account-b");
        QTRY_COMPARE(ready.size(), 3);
        QCOMPARE(network.playerBodies.size(), 2);
        QCOMPARE(errors.size(), 0);
        for (const auto &event : stages)
            QVERIFY(!event[0].toString().startsWith("helper_"));
    }
    void nativeCancellationAndClientRecovery() {
        NativeFixtureNetwork network;
        network.delay = 100;
        network.firstProbeStatus = 403;
        NativeAudioResolver resolver(nullptr, &network);
        QSignalSpy ready(&resolver, &NativeAudioResolver::ready),
            errors(&resolver, &NativeAudioResolver::unavailable);
        resolver.resolve("OLDREQUEST1");
        resolver.cancel();
        resolver.resolve("NEWREQUEST1");
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(network.playerBodies.size(), 3); // Cancelled, first client, recovery client.
        QCOMPARE(network.probes, 2);
        QCOMPARE(errors.size(), 0);
        QVERIFY(!resolver.busy());
    }
    void nativeRejectsUnsafeAndUnprocessedUrls_data() {
        QTest::addColumn<QString>("url");
        QTest::newRow("foreign") << "https://attacker.test/audio";
        QTest::newRow("plain-http") << "http://fixture.googlevideo.com/audio";
        QTest::newRow("js-required") << "https://fixture.googlevideo.com/audio?n=encrypted";
        QTest::newRow("expired") << "https://fixture.googlevideo.com/audio?expire=1";
    }
    void nativeRejectsUnsafeAndUnprocessedUrls() {
        QFETCH(QString, url);
        NativeFixtureNetwork network;
        network.source = url;
        NativeAudioResolver resolver(nullptr, &network);
        QSignalSpy ready(&resolver, &NativeAudioResolver::ready),
            errors(&resolver, &NativeAudioResolver::unavailable);
        resolver.resolve("ANYREQUEST1");
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(network.probes, 0);
    }
    void nativeCipherFallsBackToHelper() {
        NativeFixtureNetwork network;
        network.source += "?n=encrypted";
        AudioResolver resolver(nullptr, fixture(), fixture(), &network);
        QSignalSpy ready(&resolver, &AudioResolver::ready), errors(&resolver, &AudioResolver::error);
        resolver.resolve("CIPHERTEST1");
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready[0][0].toUrl().path(), QString("/videoplayback"));
        QCOMPARE(errors.size(), 0);
        QCOMPARE(network.probes, 0);
    }
    void accountFallbackAndForcedRefresh() {
        AudioResolver resolver(nullptr, fixture(), fixture());
        QSignalSpy ready(&resolver, &AudioResolver::ready), errors(&resolver, &AudioResolver::error);
        QSignalSpy hits(&resolver, &AudioResolver::cacheHit);
        const auto before =
            QDir(QDir::tempPath()).entryList({"velune-playback-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        resolver.resolve("AUTHONLY123", "# Netscape HTTP Cookie File\n");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(QDir(QDir::tempPath()).entryList({"velune-playback-*"}, QDir::Dirs | QDir::NoDotAndDotDot),
                 before);
        resolver.resolve("REFRESH1234");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
        QCOMPARE(ready.last()[0].toUrl().path(), QString("/videoplayback"));
        resolver.resolve("REFRESH1234");
        QCOMPARE(hits.size(), 1);
        QCOMPARE(ready.size(), 3);
        resolver.resolve("REFRESH1234", {}, true);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 4, 5000);
        QCOMPARE(ready.last()[0].toUrl().path(), QString("/refreshed"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(errors.size(), 0);
    }
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
#if defined(VELUNE_MULTIMEDIA) || defined(VELUNE_MPV)
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
        auto player = createPlaybackEngine();
        QVERIFY(player->available());
        QSignalSpy playbackErrors(player.get(), &IPlaybackEngine::errorOccurred);
        player->setVolume(0);
        player->load(local, 1);
        player->play();
        QTRY_VERIFY_WITH_TIMEOUT(player->position() > 500 || !playbackErrors.isEmpty(), 10000);
        QCOMPARE(playbackErrors.size(), 0);
        QVERIFY(player->position() > 500);
        qInfo() << "First playback ms:" << clock.elapsed();
        QTRY_VERIFY_WITH_TIMEOUT(player->duration() > 2000, 10000);
        const auto target = player->duration() / 2;
        player->seek(target);
        QTest::qWait(1200);
        QVERIFY2(player->position() >= target - 300 && player->position() < target + 4000,
                 "Seek must remain applied while audio continues playing.");
        const auto earlier = player->duration() / 4;
        player->seek(earlier);
        QTest::qWait(1000);
        QVERIFY(player->position() >= earlier - 300 && player->position() < earlier + 4000);
        player->pause();
        QTRY_COMPARE(player->state(), PlaybackState::Paused);
        QCOMPARE(playbackErrors.size(), 0);
        player->stop();
        player->cancel();
        stream.stop();
        resolver.cancel();
#else
        QSKIP("Native audio requires Qt Multimedia or libmpv.");
#endif
    }
};
QTEST_MAIN(AudioResolverTests)
#include "AudioResolverTests.moc"
