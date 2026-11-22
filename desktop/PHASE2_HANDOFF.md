# Phase 2 handoff — 2026-11-22

## State and preservation

Work is saved, uncommitted, on **`feat/cpp-desktop-app`** in `/home/mohamed/Desktop/Velune`.
**Do not reset, clean, discard, or blanket-commit the working tree.** Phase 1 was already
uncommitted; Android, authentication, search, library/catalog and UI changes belong to
other existing/concurrent work. Preserve them. ConnectClient and EmbeddedSession sources
were verified byte-identical to the initial snapshot. No Phase 3 work was started.

Phase 2 local implementation/report work is complete, including the missing-plugin fix
and final Linux checks described below. Windows native execution and CI remain unexecuted;
do not imply cross-platform certification. No Phase 3 work was started. This file is the
durable resume point; retain the original measurements rather than rerunning the matrix.

## Completed implementation / files

- `src/playback/MpvPlaybackEngine.h/.cpp`, `MpvApi.h`: official C API 2.2+ via QLibrary;
  one native core, audio only (`vid=no`, no window/cover art/scripts/ytdl), explicit TLS
  verification, async commands, cached getters, deferred seeking, volume-before-unpause,
  stop/restart/cancel, buffering, EOF/errors, and stale-event protection.
- Wake callback only coalesces queued Qt dispatch; bounded event batches drain on the
  QObject thread. Property/command epochs and playlist IDs fence events. Retirement uses
  permanent **`idle-active` observation plus START/END tracking**, not deprecated IDLE.
  Never force the observed idle flag false when submitting a load: cancellation before
  native START can leave the real idle property unchanged (regression test covers this).
- Destructor cancels immediately, detaches callbacks, and sends potentially blocking final
  native destruction to Qt's thread pool with callback/library storage retained.
- `PlaybackEngineFactory.h/.cpp`: `VELUNE_PLAYBACK_BACKEND=qt|mpv`, Qt default; missing or
  failed mpv initialization falls back to available Qt. No automatic media-error switching.
  `IPlaybackEngine` adds default `backendName()` and `mediaReady` signal. Fake injection remains.
- `PlaybackTrace.h/.cpp`, narrow MusicService hooks, AudioResolver `cacheHit` signal:
  opt-in `VELUNE_PLAYBACK_TRACE=1`, monotonic stage milliseconds, no URLs/credentials;
  production first-audio stays null, explicitly distinguished from position/state proxies.
- `QtPlaybackEngine` retained; adds media-ready notification, actual backend availability
  check, and explicit device injection for fair benchmarks. **Missing-plugin preflight is implemented and regression-tested.**
- Optional CMake mpv headers/API discovery, `VELUNE_ENABLE_MPV`, `MPV_ROOT`,
  `VELUNE_BUILD_BENCHMARKS`; no required mpv linker/runtime dependency. MSVC `/utf-8` added.
- `tests/MpvPlaybackTests.cpp`, `FakeMpvApi.h`, `PlaybackFixture.h`, `FixtureNetwork.h`;
  expanded `PlaybackTests.cpp`. Tests cover native decoding, stale events, queue overflow,
  rapid controls/replacements, paused/deferred seeks, buffering cancellation/destruction,
  native MusicService routing/logout. CoreTests skips native Qt audio UI check without a
  device (Linux validation supplies a null sink); Fake tests remain.
- `benchmarks/PlaybackBenchmark.cpp`, `generate-fixtures.cmake`, `run-matrix.cmake`,
  `summarize-results.cmake`, `with-null-audio.sh`: files/real production HTTP relay with
  injected upstream, cold/warm/switch/seek/resources/stability, optional PCM capture,
  controlled midstream buffering, public live YouTube explicitly gated by BOTH
  `--live-youtube-id` and `--allow-live` (not run).
- `.github/workflows/desktop.yml`: Linux build matrix; Windows 2022/MSVC/Qt 6.8.3,
  pinned source dependencies and official mpv header, compilation/deployment tests.
  Expected enabled mpv adapter is asserted. **CI has not run.**
- `scripts/deploy-windows.ps1`, installer notice copying, `THIRD_PARTY.md`,
  `WINDOWS_DEPLOYMENT.md`, README Phase 2 section, `benchmarks/RESULTS.md`.
  Runtime DLLs require audited manifests/hashes/notices; no mpv binaries bundled/downloaded at runtime.

## Saved measurements and current tests

Durable raw data is in **`benchmarks/results/2026-11-22-linux/`**:
`comparisons.json`, `supplemental.json`, `codec-output.json`, `buffering.json`,
`summary.csv`, `conditions.json`, `dependency-audit.json`, `validation.json`.

- **96 groups / 2,880 trials, zero failures**: MP3, AAC/M4A, Opus, FLAC, WAV, MP4 audio+video;
  file/localhost relay; cold/warm/switch/seek; 30 samples per cell, median/nearest-rank p95.
- **400/400 stability sequences** passed: 100 per backend/transport, including 40 pause/resume
  calls, paused seek, stop and explicit restart each.
- **300 initial PCM-output confirmations** passed: WAV n=30 per backend/transport;
  all other codecs n=3; additional MP4 n=30. Captures are isolated virtual-sink PCM receipt,
  including capture buffering, **not physical audibility**. No real account/live service used.
- WAV PCM median/p95 ms: Qt file **34.7/38.5**, mpv file **16.7/49.5**;
  Qt relay **20.5/52.5**, mpv relay **21.8/60.5**.
- Additional MP4 PCM n=30 median/p95: Qt file **237.0/265.4**, mpv **15.4/16.8**;
  Qt relay **242.8/277.3**, mpv **15.5/27.6**. Qt 6.4 headless video probing contributes;
  do not generalize to live YouTube/other versions/hardware.
- CPU/RSS: nominal 30-second windows (actual 29.776–29.954 s), **29 one-second observations**
  each. Active CPU median Qt 1.472%, mpv 1.680% of one core; active RSS Qt 133.5 MiB,
  mpv 164.9 MiB. Idle CPU ~0.027%, RSS Qt 126.3 / mpv 147.2 MiB.
- New controlled midstream stall: 20 s WAV, 16 KiB/50 ms release, upstream paused 5 s
  after initial playback. **3/3 recoveries each**, zero errors. Actual PCM gaps median/p95
  Qt **2590.1/2595.1 ms**, mpv **3104.9/3110.1 ms**. Initial prefill differs; do not interpret
  gap lengths as a universal buffering winner. mpv reported buffering progress/recovery;
  Qt reported no buffering-progress episodes despite measured PCM starvation.
- Linux full Release, full Debug, Qt-only, minimal (both playback libs/Keychain disabled),
  mpv-only (Qt Multimedia/Keychain disabled), and automatic absent-mpv-header discovery:
  **builds passed, all 8 CTest targets passed in every configuration**.
  Latest full-suite logs: `/tmp/velune-phase2-results/ctest-{release,debug,qt-only,minimal,mpv-only,no-headers}.log`.
  Final resume checks include the streaming fixture and missing-plugin regressions;
  all six configurations again pass all eight targets, with benchmarks built in each.
- Missing mpv runtime: verified diagnostic falls back to Qt; benchmark correctly refuses
  a substituted backend (exit 3). Missing live opt-in rejected (exit 2).
- Intermediate concurrent library/UI compilation/test failures were resolved in that work;
  one Debug UI segfault in `layoutAndLoginGate` was not reproduced under isolated gdb or
  later full suites. Do not claim its cause was established.
- Windows compilation/runtime/packaging, physical devices/speakers and live YouTube:
  **not executed**. Recommendation remains **Qt default**, mpv useful opt-in especially for
  the tested video-container path. No aggressive buffer/prefetch/extractor tuning retained.

## Resolved startup bug and final evidence

The forced missing-plugin command previously aborted in Qt 6.4 before the availability
check. `QtPlaybackEngine` now checks public QPluginLoader metadata and loadability before
constructing QAudioOutput/QMediaPlayer. It respects explicit `QT_MEDIA_BACKEND`, scans
`QCoreApplication::libraryPaths()/multimedia`, and supports static metadata. Failed
preflight leaves Backend null; all controls/getters/device injection remain safe. Loading
emits request-tagged Unavailable/error with installation/environment guidance. The Phase 1
video sink workaround is retained.

Fresh-process regressions cover empty plugin paths with explicit/default selection,
unknown backend selection, and a Linux plugin whose dependency was deliberately made
unresolvable. The original reproduction now passes. Successful default selection with
`QT_MEDIA_BACKEND` unset is also checked. With no explicit backend, preflight conservatively
requires every discovered candidate to load before leaving selection to Qt. A broken unused
candidate may disable Qt playback; selecting a known healthy backend avoids this. Static
plugin execution and failures inside otherwise loadable integration factories remain untested.

Original 96-group/2,880-trial comparisons and supplemental data were preserved. Only the
changed successful Qt startup path was remeasured: fresh-process WAV n=30 **213.4/229.1 ms**
median/p95, zero failures (original **210.5/232.3 ms**). Paired constructor probes, n=30 each,
with/bypassing preflight measured **16.737/17.402 ms** versus **16.750/19.603 ms**. This
interactive-host check shows no clear startup penalty; it is not a significance claim.
Raw targeted timings are `startup-preflight.json` and `qt-preflight-cold.json` in the saved
results directory. Original collection fingerprints remain in `conditions.json`; final
source/binary fingerprints and durable logs are recorded separately in `validation.json`.

RESULTS.md now reports the saved MP4 n=30 PCM checks and real midstream buffering results.
README includes the buffering reproduction command and PCM-gap interpretation.
The benchmark's Windows header order is corrected (windows.h before psapi.h). Workflow
PowerShell native commands now fail on errors. Local Windows-related checks include YAML,
actionlint 1.7.7, PowerShell 7.4.13 parsing of the deployment script/all workflow steps,
manifest rejection checks (bad hash, parent destination, missing notice), and verification
that pinned dependency commits exist. These are Linux-host checks, not a Windows build.

## Exact resume commands / environment

Host dependencies were extracted from Ubuntu packages, **not installed system-wide**:

- Qt/Keychain/FFmpeg staged prefix: `/tmp/velune-desktop-deps/root/usr`
- mpv 0.37.0 and test audio tools: `/tmp/velune-phase2-deps/root/usr`
- `/tmp/velune-phase2-env.sh` exports LD_LIBRARY_PATH, QT_PLUGIN_PATH,
  QT_MEDIA_BACKEND=ffmpeg, headless WebEngine flags/process/resources/locales.
- `/tmp/velune-phase2-bin/` contains wrappers for staged PulseAudio/pactl/parec.
- `/tmp/velune-phase2-results/` retains per-run JSON/logs, fixtures and scripts.
  Initial snapshot: `desktop-before.tgz`, preexisting git patch: `preexisting.patch`.
  If /tmp disappears, use README dependency/fixture commands; all final raw results are in-tree.

```sh
cd /home/mohamed/Desktop/Velune
source /tmp/velune-phase2-env.sh
export PATH=/tmp/velune-phase2-bin:$PATH
# Host-specific script configures all staged Qt component paths and validates six variants:
desktop/benchmarks/with-null-audio.sh bash /tmp/velune-phase2-build-matrix.sh
# Fast current Release/native check after changing the missing-plugin handling:
cmake --build /tmp/velune-phase2-release -j2
VELUNE_CODEC_FIXTURES=/tmp/velune-phase2-results/fixtures \
 desktop/benchmarks/with-null-audio.sh \
 ctest --test-dir /tmp/velune-phase2-release --output-on-failure --timeout 90
# Targeted native check (18 QTest passes including init/cleanup before latest fixture addition):
VELUNE_CODEC_FIXTURES=/tmp/velune-phase2-results/fixtures \
 desktop/benchmarks/with-null-audio.sh /tmp/velune-phase2-release/velune_mpv_tests -platform offscreen
```

Portable benchmark reproduction is in README and `run-matrix.cmake`. Existing local
`/tmp/velune-phase2-final-bench.sh` runs main matrix + WAV capture + resources + stability;
`/tmp/velune-phase2-extra-bench.sh` runs buffering and MP4 n=30 output. They must run inside
`with-null-audio.sh`. Do not run builds/audio tests concurrently with timing collection.
The wrapper uses an isolated server with `--disallow-exit`; an earlier unsupervised server
exited and invalidated a run. Invalidated JSON is excluded from committed reports.

### Final state / future work

- All eight CTest targets pass in Release, Debug, Qt-only, minimal, mpv-only and absent-header
  configurations. The benchmark builds in every configuration. Final Release native tests
  include 18 QTest passes and playback tests 19 passes; live tests intentionally skip.
- `git diff --check`, shell syntax, workflow lint/YAML, and PowerShell checks pass.
  Durable resume logs are under `benchmarks/results/2026-11-22-linux/final-validation/`;
  host logs use `/tmp/velune-phase2-results/resume-*`.
- No commit, push, reset, clean, package publication, account login, or Phase 3 work occurred.
  Qt remains default. All unrelated existing changes remain in place.
- Windows/MSVC CI, native Windows playback/DLL loading, actual Windows staging and a clean
  machine smoke test still need a Windows runner and audited runtimes. CI was not dispatched.
  Live YouTube, physical audio devices and Android/TV interoperability remain untested.
- The temporary host PulseAudio null sink was already verified/unloaded at the earlier
  handoff. Resume tests used isolated servers whose wrappers cleaned them up; do not alter
  the user's audio server or sink configuration.
