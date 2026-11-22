# Phase 2 playback measurements — Linux

**Keep Qt as the default.** libmpv is a working opt-in alternative. It improved some medians and the audio/video-container path in this environment, but did not provide a consistent first-output, tail-latency, or resource advantage. Windows native validation is still outstanding.

## Conditions and measurement boundaries

Ubuntu 24.04.3 x86_64, GCC 13.3.0, Qt 6.4.2, Ubuntu libmpv 0.37.0-1ubuntu4, FFmpeg 6.1.1, Release `-O3 -DNDEBUG`. All media is generated synthetic audio; no account credentials, signed URLs, or live service were used. Exact package versions/hashes, runtime license/configuration evidence, source/binary fingerprints and fixture hashes are in [conditions.json](results/2026-11-22-linux/conditions.json) and [dependency-audit.json](results/2026-11-22-linux/dependency-audit.json).

Both backends used the same supervised, isolated PulseAudio 16.1 null sink, 48 kHz stereo, `norewinds=1`. This clocked virtual sink has a 50 ms maximum block and is **not a physical audio device**. Native backend buffering defaults were retained. `parec` requested 5 ms capture/process intervals; output confirmation means receipt of a nonzero synthetic PCM sample, including capture buffering. It does not confirm speaker output or human audibility. Desktop background load was not exclusively controlled.

- Cold: a fresh benchmark process, timed from `main()` to advancing position. OS file caches are retained; dynamic-loader time before `main()` and the complete Velune UI/login startup are excluded.
- Warm/switch: request to positive position while Playing; native notification cadence affects this proxy.
- Seek: request to position beyond the target that continues advancing. This is a position proxy, not an audio-output seek measurement.
- Relay: the production `AudioStream` and byte-range handling, with deterministic injected upstream replies. The compiled native resolver fixture measures helper startup separately; it is not real yt-dlp/Deno or network latency.
- Production `MusicService` tracing separately covers the real application request pipeline; its first-audio field remains unavailable rather than equating a state transition with sound.

Each comparison cell below has **30 successful samples**, median / nearest-rank p95 in milliseconds. **All 96 groups / 2,880 trials passed**, including MP3, AAC/M4A, Opus, FLAC, WAV, and MP4 containing audio plus video.

## Startup, switching and seeking proxies

WAV, direct file:

| Scenario | Qt median / p95 ms | mpv median / p95 ms |
| --- | ---: | ---: |
| Fresh process | 210.5 / 232.3 | 209.3 / 239.7 |
| Warm | 131.9 / 132.8 | 87.4 / 131.6 |
| Switch | 132.7 / 133.8 | 99.9 / 149.8 |
| Seek to advancing position | 100.6 / 100.8 | 75.6 / 101.1 |

Warm startup by format (position proxy):

| Format | Qt file | mpv file | Qt relay | mpv relay |
| --- | ---: | ---: | ---: | ---: |
| MP3 | 102.0 / 102.9 | 79.1 / 129.4 | 115.0 / 116.3 | 79.2 / 129.0 |
| AAC/M4A | 103.7 / 104.3 | 60.0 / 111.0 | 117.6 / 118.2 | 76.3 / 123.6 |
| Opus | 103.0 / 103.4 | 63.1 / 104.8 | 118.1 / 119.6 | 75.0 / 98.3 |
| FLAC | 103.4 / 203.8 | 77.4 / 98.0 | 115.9 / 116.2 | 78.9 / 96.3 |
| WAV | 131.9 / 132.8 | 87.4 / 131.6 | 133.4 / 146.7 | 96.2 / 129.5 |
| MP4 audio + video | 295.8 / 357.9 | 61.9 / 111.2 | 281.3 / 325.9 | 79.0 / 107.0 |

Full cold/warm/switch/seek results for every format and transport are in [summary.csv](results/2026-11-22-linux/summary.csv); all raw samples are in [comparisons.json](results/2026-11-22-linux/comparisons.json).

## Confirmed virtual-sink output

WAV, **30 captured-output samples per cell**, median / p95 milliseconds from the request:

| Transport | Qt PCM receipt | mpv PCM receipt |
| --- | ---: | ---: |
| file | 34.7 / 38.5 | 16.7 / 49.5 |
| relay | 20.5 / 52.5 | 21.8 / 60.5 |

mpv had a lower local WAV median, while Qt had a lower local p95. Relay medians were close and Qt had the lower p95. The sink clock/capture model limits interpretation of small differences; these figures do not establish a hardware-wide winner.

An additional **60/60 output checks** confirmed nonzero PCM for MP3, AAC/M4A, Opus, FLAC, and audio/video MP4 with both backends and transports (three samples per combination). Raw checks: [codec-output.json](results/2026-11-22-linux/codec-output.json). These small samples validate decoding/output, not stable p95 estimates. A separate **120/120 MP4 PCM checks (n=30 per cell)** in [supplemental.json](results/2026-11-22-linux/supplemental.json) give the following larger-sample result for this headless Qt 6.4 environment:

| MP4 output check, n=30 | Qt median / p95 ms | mpv median / p95 ms |
| --- | ---: | ---: |
| file | 237.0 / 265.4 | 15.4 / 16.8 |
| relay | 242.8 / 277.3 | 15.5 / 27.6 |

Qt emitted hardware-probe/software-fallback diagnostics for the video-containing fixture. Its Phase 1 video-sink workaround is preserved. mpv disables video before loading. This shows a useful container-specific benefit here; it is not a live YouTube measurement or a result for other Qt versions/platforms.

## CPU and memory

Benchmark-process usage, excluding the separate audio server and full UI. Idle means a warmed instance after cancel; active means decoding/playing a long generated WAV. One-second observations in nominal 30-second windows; actual windows were 29.776–29.954 seconds, **29 observations each**. CPU is percent of one logical core; RSS includes shared resident pages and common harness/fixture overhead, not peak memory.

| Backend / state | CPU median / p95, % one core | RSS median / p95 MiB |
| --- | ---: | ---: |
| qt / idle | 0.027 / 0.033 | 126.3 / 126.3 |
| qt / active_wav | 1.472 / 1.582 | 133.5 / 133.5 |
| mpv / idle | 0.027 / 0.033 | 147.2 / 147.2 |
| mpv / active_wav | 1.680 / 1.822 | 164.9 / 164.9 |

Qt used less resident memory and somewhat less active CPU in this harness. Both were effectively idle between operations.

## Resolver, relay, buffering and stability

- qt: resolver fixture median / p95 **10.99 / 11.37 ms**; relay-listening setup after resolution **0.32 / 0.39 ms**, n=180. This does not include upstream service latency or prove audio bytes have arrived.
- mpv: resolver fixture median / p95 **11.04 / 11.32 ms**; relay-listening setup after resolution **0.33 / 0.40 ms**, n=180. This does not include upstream service latency or prove audio bytes have arrived.
- qt: a configured 250 ms upstream response delay produced 622.7 / 642.5 ms request-to-position median / p95, n=30; no failures.
- mpv: a configured 250 ms upstream response delay produced 329.7 / 379.9 ms request-to-position median / p95, n=30; no failures.

The short fast fixtures reported 0 buffering-progress episodes in the main matrix. Delayed headers exercise startup waiting. A separate controlled midstream test used a 20-second WAV, released in 16 KiB chunks every 50 ms, with a 5-second upstream pause after advancing position and initial nonzero PCM output. Both backends recovered in **3/3 trials each**, with zero errors. [buffering.json](results/2026-11-22-linux/buffering.json) records the samples.

| Backend | Measured PCM gap median / p95 ms | Recovered trials |
| --- | ---: | ---: |
| Qt | 2590.1 / 2595.1 | 3/3 |
| mpv | 3104.9 / 3110.1 | 3/3 |

Gap detection requires at least 300 ms without nonzero PCM and ends at the next nonzero sample. Backend prefill differs, so the PCM gaps are shorter than the upstream pause and do not establish a universal buffering winner. mpv emitted buffering progress/recovery in each trial; Qt emitted no buffering-progress episodes despite measured PCM starvation. This is one deterministic outage pattern with a small sample, not broad adverse-network coverage. Native-event tests cover normalized notifications and cancellation; stalled relay tests cover stop and destruction.

**400/400 stability sequences passed** (100 per backend/transport), each including 40 pause/resume calls, paused seeking, stop with retained media, and explicit restart. No playback errors were recorded. [supplemental.json](results/2026-11-22-linux/supplemental.json) contains these sequences, the delayed-relay trials, output captures and CPU/RSS observations.

## Engineering decision

Keep Qt as the default/recommended general backend. Offer `VELUNE_PLAYBACK_BACKEND=mpv` for the validated Linux alternative, including the demonstrated audio/video-container benefit. Do not generalize the earlier mpv position notifications into a first-audio claim. Windows native playback, other mpv/Qt builds, real speakers, broader adverse-network conditions and live YouTube need separate validation.

No buffering, network-request, resolver algorithm, prefetch-timer or permanent-cache tuning was retained. Relay listening setup was small relative to backend/output timing; this evidence does not justify rewriting that path or reducing buffering at the expense of stalls. The native engine uses one instance and coalesced asynchronous controls for lifecycle reliability.

## Build and test validation

Final build/test results are recorded in [validation.json](results/2026-11-22-linux/validation.json). Windows CI and packaging support are added, but were not executed locally; no Windows compilation or runtime pass is claimed. Live tests remain opt-in and were not run. Connect protocol and authentication source files are unchanged from the inspected Phase 1 working tree; fake/native routing checks and TLS/session/ACK loopback tests provide automated compatibility evidence, not physical Android/TV interoperability certification.

An initial comparison attempt was invalidated by an exited test audio server. The reported data comes entirely from the completed supervised-server run. Intermediate failures while concurrent library/UI work was arriving, and a transient Debug UI segfault, are distinguished in the validation record rather than attributed to playback without evidence.

Collection fingerprints describe the measured binary. Final validation additionally includes correctness-only handling for unavailable/error states and explicit direct-HTTPS certificate verification; buffering settings are unchanged. The later missing-plugin preflight also affects successful Qt startup; its targeted timing check is recorded separately below, without replacing the original comparison matrix.


## Final Qt startup correction

Public QtCore plugin metadata/load checks now run before Qt Multimedia objects are
constructed. Missing plugins, missing transitive dependencies, and unknown explicit
backend selections report an unavailable engine instead of the reproduced Qt 6.4 abort.
Fresh-process regressions cover these cases and all unavailable controls/getters.
Without an explicit backend, preflight conservatively requires all candidates to load
and then lets Qt select its platform default; an unused broken candidate can disable
local Qt playback. Static plugin execution and failures within a loadable plugin's
integration factory have not been validated.

The changed successful startup path received a targeted check, separately from the
original comparative matrix: **30/30 Qt fresh-process WAV/file trials passed**, median /
p95 **213.4 / 229.1 ms** (original **210.5 / 232.3 ms**). The same position proxy and retained
OS caches apply. Raw data: [qt-preflight-cold.json](results/2026-11-22-linux/qt-preflight-cold.json).

A constructor-only A/B probe used identical final source with the preflight call enabled
or bypassed, alternating order in 30 fresh-process pairs. Enabled median/p95 was
**16.737 / 17.402 ms**; bypassed **16.750 / 19.603 ms**. Timing starts after QApplication
and excludes loader/application setup. Both include the plugin loading Qt otherwise does.
The interactive-host observations show no clear startup penalty and support no statistical
significance claim. [startup-preflight.json](results/2026-11-22-linux/startup-preflight.json)
contains method, fingerprints and samples. No full comparison matrix was repeated.

Final Linux builds include the benchmark and all eight CTest targets in all six
configurations. The original missing-plugin reproduction passes, along with the updated
native/relay regressions. Windows-related local evidence comprises workflow lint/YAML,
PowerShell parsing and manifest rejection checks on Linux; MSVC compilation, Windows CI,
DLL loading, native playback and real deployment remain unexecuted.
