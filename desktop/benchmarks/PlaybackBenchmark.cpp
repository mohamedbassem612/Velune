#include "FixtureNetwork.h"
#include "PlaybackFixture.h"
#include "playback/MpvPlaybackEngine.h"
#include "playback/PlaybackEngineFactory.h"
#include "services/AudioResolver.h"
#include "services/AudioStream.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <unistd.h>
#endif
using namespace velune;
static double cpuMilliseconds() {
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return -1;
    auto ticks = [](FILETIME f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return (ticks(kernel) + ticks(user)) / 10000.;
#else
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value))
        return -1;
    return value.tv_sec * 1000. + value.tv_nsec / 1000000.;
#endif
}
static double residentMiB() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS value{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &value, sizeof(value)))
        return -1;
    return value.WorkingSetSize / 1048576.;
#else
    QFile file("/proc/self/statm");
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    return file.readAll().simplified().split(' ').value(1).toDouble() * sysconf(_SC_PAGESIZE) / 1048576.;
#endif
}
static bool awaitEngine(IPlaybackEngine &engine, const std::function<bool()> &predicate,
                        int timeout = 10000) {
    if (predicate())
        return true;
    QEventLoop loop;
    auto check = [&] {
        if (predicate() || engine.state() == PlaybackState::Error)
            loop.quit();
    };
    QObject::connect(&engine, &IPlaybackEngine::positionChanged, &loop, check);
    QObject::connect(&engine, &IPlaybackEngine::stateChanged, &loop, check);
    QObject::connect(&engine, &IPlaybackEngine::mediaReady, &loop, check);
    QObject::connect(&engine, &IPlaybackEngine::durationChanged, &loop, check);
    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    loop.exec();
    return predicate();
}
static void delay(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static QJsonObject summarize(const QJsonArray &rows, const QString &key, const QString &unit = "ms") {
    QList<double> values;
    for (auto row : rows) {
        const auto object = row.toObject();
        if (object["success"].toBool() && object[key].isDouble())
            values.append(object[key].toDouble());
    }
    std::sort(values.begin(), values.end());
    if (values.isEmpty())
        return {{"samples", 0}, {"median_" + unit, QJsonValue::Null}, {"p95_" + unit, QJsonValue::Null}};
    const auto median = values.size() % 2 ? values[values.size() / 2]
                                          : (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2;
    return {{"samples", values.size()},
            {"median_" + unit, median},
            {"p95_" + unit, values[qCeil(values.size() * .95) - 1]}};
}
int main(int argc, char **argv) {
    QElapsedTimer processClock;
    processClock.start();
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("velune-playback-benchmark");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions(
        {{"backend", "qt or mpv (no fallback accepted)", "backend", "qt"},
         {"fixture", "Audio fixture path", "path"},
         {"transport", "file or relay", "mode", "file"},
         {"scenario", "cold, warm, switch, seek, resources, stability, buffering, or once", "name", "warm"},
         {"iterations", "Number of iterations", "count", "30"},
         {"output", "JSON output path", "path"},
         {"resolver-fixture", "Deterministic resolver helper (relay only)", "path"},
         {"resource-seconds", "Resource measurement window", "seconds", "30"},
         {"upstream-delay-ms", "Deterministic upstream response delay", "ms", "0"},
         {"pulse-monitor", "Explicit isolated PulseAudio monitor name (warm/once only)", "name"},
         {"parec", "PulseAudio capture executable", "path", "parec"},
         {"audio-device", "Explicit audio device ID (Linux: PulseAudio sink name)", "id"},
         {"live-youtube-id", "Opt-in public video ID; requires --allow-live", "id"},
         {"allow-live",
          "Explicitly enable external YouTube resolution/streaming without account credentials"}});
    parser.process(app);
    const bool live = parser.isSet("live-youtube-id");
    const auto backend = parser.value("backend"), scenario = parser.value("scenario"),
               transport = live && !parser.isSet("transport") ? QString("relay") : parser.value("transport");
    const auto fixture = QFileInfo(parser.value("fixture")).absoluteFilePath();
    bool countOK = false;
    const int iterations = parser.value("iterations").toInt(&countOK);
    if (!countOK || iterations < 1 || iterations > 10000 || (!live && !QFileInfo(fixture).isFile()) ||
        (live && (!parser.isSet("allow-live") || transport != "relay" || scenario == "resources" ||
                  parser.isSet("resolver-fixture"))) ||
        (backend != "qt" && backend != "mpv") || (transport != "file" && transport != "relay") ||
        !QStringList{"cold", "warm", "switch", "seek", "resources", "stability", "buffering", "once"}
             .contains(scenario) ||
        (scenario == "buffering" && (live || transport != "relay" || !parser.isSet("pulse-monitor") ||
                                     QFileInfo(fixture).suffix() != "wav"))) {
        QTextStream(stderr) << "Invalid benchmark configuration\n";
        return 2;
    }
    QJsonArray rows;
    QJsonObject resources;
    int failures = 0;
    if (parser.isSet("pulse-monitor") && scenario != "warm" && scenario != "once" &&
        scenario != "buffering") {
        QTextStream(stderr)
            << "Output capture supports warm/once only; switching needs distinct source markers\n";
        return 2;
    }
    if (scenario == "cold") {
        for (int i = 0; i < iterations; ++i) {
            QProcess child;
            QStringList args{"--backend",           backend,
                             "--fixture",           fixture,
                             "--transport",         transport,
                             "--scenario",          "once",
                             "--iterations",        "1",
                             "-platform",           "offscreen",
                             "--upstream-delay-ms", parser.value("upstream-delay-ms")};
            if (parser.isSet("resolver-fixture"))
                args << "--resolver-fixture" << parser.value("resolver-fixture");
            if (parser.isSet("audio-device"))
                args << "--audio-device" << parser.value("audio-device");
            if (live)
                args << "--live-youtube-id" << parser.value("live-youtube-id") << "--allow-live";
            child.start(QCoreApplication::applicationFilePath(), args);
            if (!child.waitForFinished(live ? 140000 : 20000)) {
                child.kill();
                child.waitForFinished();
            }
            const auto result = QJsonDocument::fromJson(child.readAllStandardOutput()).object();
            const auto samples = result["raw_samples"].toArray();
            auto sample = samples.isEmpty() ? QJsonObject{} : samples[0].toObject();
            if (child.exitCode() || sample.isEmpty()) {
                sample = {{"success", false}};
                ++failures;
            }
            rows.append(sample);
        }
    } else {
        auto selected = selectPlaybackEngine(backend, parser.value("audio-device").toUtf8());
        if (!selected.engine->available() || selected.engine->backendName() != backend) {
            QTextStream(stderr) << "Requested benchmark backend unavailable: " << selected.diagnostic << '\n';
            return 3;
        }
        auto &engine = *selected.engine;
        // A virtual output is selected externally for both engines; no production tuning here.
        FixtureNetwork network;
        QFile file(fixture);
        if (!live && !file.open(QIODevice::ReadOnly))
            return 2;
        if (!live)
            network.bytes = file.readAll();
        network.delay = qBound(0, parser.value("upstream-delay-ms").toInt(), 9000);
        if (scenario == "buffering")
            network.chunkBytes = 16384;
        AudioStream relay(nullptr, live ? nullptr : &network);
        const auto helper = parser.value("resolver-fixture");
        AudioResolver resolver(nullptr, helper, helper);
        PlaybackRequest request = 0;
        const int count = scenario == "once" || scenario == "resources" ? 1 : iterations;
        for (int i = 0; i < count; ++i) {
            if (scenario != "switch") {
                engine.cancel();
                delay(20);
            }
            QJsonObject sample{{"success", false}, {"first_audio_output_ms", QJsonValue::Null}};
            QElapsedTimer clock;
            auto elapsed = [&] { return clock.nsecsElapsed() / 1000000.; };
            auto url = QUrl::fromLocalFile(fixture);
            QObject scope;
            QProcess monitor;
            bool armed = false;
            double lastPcm = -1, gapStart = -1;
            if (parser.isSet("pulse-monitor")) {
                monitor.start(parser.value("parec"),
                              {"--device=" + parser.value("pulse-monitor"), "--raw", "--format=s16le",
                               "--rate=48000", "--channels=1", "--latency-msec=5", "--process-time-msec=5"});
                if (!monitor.waitForStarted(2000)) {
                    QTextStream(stderr) << "Cannot start configured output capture\n";
                    return 2;
                }
                QObject::connect(&monitor, &QProcess::readyReadStandardOutput, &scope, [&] {
                    const auto pcm = monitor.readAllStandardOutput();
                    if (!armed || (scenario != "buffering" && !sample["first_audio_output_ms"].isNull()))
                        return;
                    for (qsizetype offset = 0; offset + 1 < pcm.size(); offset += 2)
                        if (qAbs(int(qFromLittleEndian<qint16>(pcm.constData() + offset))) > 10) {
                            const auto now = elapsed();
                            if (sample["first_audio_output_ms"].isNull())
                                sample["first_audio_output_ms"] = now;
                            if (gapStart >= 0 && !sample.contains("pcm_gap_ms")) {
                                sample["pcm_gap_ms"] = now - gapStart;
                                sample["pcm_recovered_ms"] = now;
                            }
                            lastPcm = now;
                            break;
                        }
                });
                delay(250); // Drain the retired track and establish capture before starting the clock.
                monitor.readAllStandardOutput();
            }
            clock.start();
            armed = true;
            int errors = 0, buffering = 0;
            double bufferingStart = -1, bufferingRecovery = 0;
            const auto id = ++request;
            QObject::connect(&engine, &IPlaybackEngine::mediaReady, &scope, [&](PlaybackRequest r) {
                if (r == id && !sample.contains("playable_ms"))
                    sample["playable_ms"] = elapsed();
            });
            QObject::connect(&engine, &IPlaybackEngine::stateChanged, &scope,
                             [&](PlaybackRequest r, PlaybackState s) {
                                 if (r == id && s == PlaybackState::Playing && !sample.contains("active_ms"))
                                     sample["active_ms"] = elapsed();
                             });
            QObject::connect(&engine, &IPlaybackEngine::errorOccurred, &scope, [&](PlaybackRequest r) {
                if (r == id)
                    ++errors;
            });
            QObject::connect(&engine, &IPlaybackEngine::bufferingChanged, &scope,
                             [&](PlaybackRequest r, float progress) {
                                 if (r != id)
                                     return;
                                 if (progress < 1 && bufferingStart < 0) {
                                     ++buffering;
                                     bufferingStart = elapsed();
                                 }
                                 if (progress >= 1 && bufferingStart >= 0) {
                                     bufferingRecovery += elapsed() - bufferingStart;
                                     bufferingStart = -1;
                                 }
                             });
            if (transport == "relay") {
                if (live || !helper.isEmpty()) {
                    QEventLoop loop;
                    bool resolved = false;
                    sample["resolver_started_ms"] = elapsed();
                    QObject::connect(&resolver, &AudioResolver::ready, &scope,
                                     [&](QUrl source, QJsonObject headers) {
                                         sample["resolver_completed_ms"] = elapsed();
                                         resolved = true;
                                         url = relay.start(source, headers, id);
                                         loop.quit();
                                     });
                    QObject::connect(&resolver, &AudioResolver::error, &loop, &QEventLoop::quit);
                    resolver.resolve(live ? parser.value("live-youtube-id") : QString("GOODGOODGOO"), {},
                                     !live);
                    if (!resolved) {
                        QTimer::singleShot(live ? 125000 : 5000, &loop, &QEventLoop::quit);
                        loop.exec();
                    }
                    if (!resolved) {
                        rows.append(sample);
                        ++failures;
                        continue;
                    }
                } else
                    url = relay.start(QUrl("https://fixture.googlevideo.com/audio"), {}, id);
                sample["relay_ready_ms"] = elapsed();
            }
            sample["backend_load_ms"] = elapsed();
            engine.load(url, id);
            engine.play();
            bool ok = awaitEngine(
                engine, [&] { return engine.state() == PlaybackState::Playing && engine.position() > 0; });
            sample["first_position_proxy_ms"] = elapsed();
            sample["main_to_first_position_ms"] = processClock.nsecsElapsed() / 1000000.;
            sample["backend_to_first_position_proxy_ms"] =
                sample["first_position_proxy_ms"].toDouble() - sample["backend_load_ms"].toDouble();
            if (sample.contains("resolver_completed_ms"))
                sample["resolver_duration_ms"] =
                    sample["resolver_completed_ms"].toDouble() - sample["resolver_started_ms"].toDouble();
            if (parser.isSet("pulse-monitor")) {
                if (sample["first_audio_output_ms"].isNull()) {
                    QEventLoop outputLoop;
                    QObject::connect(&monitor, &QProcess::readyReadStandardOutput, &outputLoop, [&] {
                        if (!sample["first_audio_output_ms"].isNull())
                            outputLoop.quit();
                    });
                    QTimer::singleShot(5000, &outputLoop, &QEventLoop::quit);
                    outputLoop.exec();
                }
                ok = ok && !sample["first_audio_output_ms"].isNull();
                if (scenario != "buffering") {
                    monitor.kill();
                    monitor.waitForFinished(2000);
                }
            }
            if (ok && scenario == "buffering") {
                ok = engine.duration() >= 10000;
                if (ok) {
                    sample["upstream_stall_requested_ms"] = elapsed();
                    sample["upstream_stall_duration_ms"] = 5000;
                    QMetaObject::invokeMethod(
                        &network, [&network] { network.blockFor(5000); }, Qt::QueuedConnection);
                    QEventLoop recovery;
                    QTimer watch;
                    watch.setTimerType(Qt::PreciseTimer);
                    QObject::connect(&watch, &QTimer::timeout, &recovery, [&] {
                        if (gapStart < 0 && lastPcm >= 0 && elapsed() - lastPcm > 300)
                            gapStart = lastPcm;
                        if (sample.contains("pcm_gap_ms") || errors)
                            recovery.quit();
                    });
                    watch.start(50);
                    QTimer::singleShot(12000, &recovery, &QEventLoop::quit);
                    recovery.exec();
                    ok = sample.contains("pcm_gap_ms");
                }
                monitor.kill();
                monitor.waitForFinished(2000);
            }
            if (ok && scenario == "seek") {
                const auto target = engine.duration() / 2;
                clock.restart();
                engine.seek(target);
                ok = target > 0 && awaitEngine(engine, [&] {
                         return engine.position() > target + 20 && engine.position() < target + 1500 &&
                                engine.state() == PlaybackState::Playing;
                     });
                sample["seek_position_advancing_proxy_ms"] = elapsed();
            }
            if (ok && scenario == "stability") {
                for (int spam = 0; spam < 20; ++spam) {
                    engine.pause();
                    engine.resume();
                }
                engine.pause();
                ok = awaitEngine(engine, [&] { return engine.state() == PlaybackState::Paused; });
                const auto target = engine.duration() / 2;
                engine.seek(target);
                ok = ok && awaitEngine(engine, [&] {
                         return qAbs(engine.position() - target) < 150 &&
                                engine.state() == PlaybackState::Paused;
                     });
                engine.resume();
                ok = ok && awaitEngine(engine, [&] { return engine.state() == PlaybackState::Playing; });
                engine.stop();
                ok = ok && engine.state() == PlaybackState::Stopped && !engine.source().isEmpty();
                engine.resume();
                ok = ok && awaitEngine(engine, [&] {
                         return engine.state() == PlaybackState::Playing && engine.position() > 0 &&
                                engine.position() < 1000;
                     });
            }
            sample["errors"] = errors;
            sample["buffering_episodes"] = buffering;
            sample["buffering_recovery_ms"] = bufferingRecovery;
            sample["buffering_unrecovered"] = bufferingStart >= 0;
            sample["success"] = ok && !errors;
            if (!ok || errors)
                ++failures;
            rows.append(sample);
        }
        if (scenario == "resources") {
            engine.cancel();
            delay(250);
            const int seconds = qBound(1, parser.value("resource-seconds").toInt(), 120);
            auto measure = [&] {
                const auto cpu = cpuMilliseconds();
                QElapsedTimer wall;
                wall.start();
                const auto before = residentMiB();
                QJsonArray samples;
                QEventLoop loop;
                QTimer timer;
                timer.setTimerType(Qt::PreciseTimer);
                qint64 lastWall = 0;
                double lastCpu = cpu;
                QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
                    const auto nowWall = wall.elapsed();
                    const auto nowCpu = cpuMilliseconds();
                    samples.append(QJsonObject{{"success", true},
                                               {"at_ms", nowWall},
                                               {"cpu", (nowCpu - lastCpu) / (nowWall - lastWall) * 100},
                                               {"rss", residentMiB()}});
                    lastWall = nowWall;
                    lastCpu = nowCpu;
                });
                timer.start(1000);
                QTimer::singleShot(seconds * 1000 + 10, &loop, &QEventLoop::quit);
                loop.exec();
                return QJsonObject{{"window_ms", wall.elapsed()},
                                   {"cpu_one_core_percent", (cpuMilliseconds() - cpu) / wall.elapsed() * 100},
                                   {"rss_before_mib", before},
                                   {"rss_after_mib", residentMiB()},
                                   {"samples", samples},
                                   {"cpu_distribution", summarize(samples, "cpu", "percent")},
                                   {"rss_distribution", summarize(samples, "rss", "mib")}};
            };
            resources["idle"] = measure();
            QTemporaryDir temporary;
            const auto longWav = temporary.filePath("resource.wav");
            writePlaybackWav(longWav, (seconds + 5) * 1000);
            engine.load(QUrl::fromLocalFile(longWav), ++request);
            engine.play();
            if (!awaitEngine(engine, [&] { return engine.position() > 0; }))
                ++failures;
            resources["active_wav"] = measure();
        }
        engine.cancel();
        relay.stop();
    }
    const auto metric = scenario == "buffering" ? "pcm_gap_ms"
                        : scenario == "seek"    ? "seek_position_advancing_proxy_ms"
                        : scenario == "cold"    ? "main_to_first_position_ms"
                                                : "first_position_proxy_ms";
    QJsonObject stageSummaries;
    for (const auto key : {"playable_ms", "active_ms", "backend_to_first_position_proxy_ms",
                           "resolver_duration_ms", "first_audio_output_ms"})
        stageSummaries[key] = summarize(rows, key);
    QJsonObject result{
        {"schema", 1},
        {"backend", backend},
        {"scenario", scenario},
        {"transport", transport},
        {"format", live ? QString("live") : QFileInfo(fixture).suffix()},
        {"live_service", live},
        {"qt_version", qVersion()},
        {"os", QSysInfo::prettyProductName()},
        {"cpu_arch", QSysInfo::currentCpuArchitecture()},
        {"iterations", rows.size()},
        {"failures", failures},
        {"metric", metric},
        {"summary", summarize(rows, metric)},
        {"stage_summaries", stageSummaries},
        {"raw_samples", rows},
        {"resources", resources},
        {"output_capture", parser.isSet("pulse-monitor")
                               ? "isolated sink nonzero PCM receipt; includes capture buffering, not "
                                 "speaker/audibility confirmation"
                               : "unavailable"},
        {"limitations",
         "Position/state proxies do not confirm audible output. Cold = fresh process, OS cache retained. "
         "Seek waits for natural position advance; notification cadence affects results."}};
    const auto bytes = QJsonDocument(result).toJson();
    if (parser.isSet("output")) {
        QFile out(parser.value("output"));
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
            return 2;
    } else {
        QFile out;
        out.open(stdout, QIODevice::WriteOnly);
        out.write(bytes);
        out.flush();
    }
    return failures ? 1 : 0;
}
