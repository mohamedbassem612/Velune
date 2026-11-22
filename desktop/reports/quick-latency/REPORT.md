# Quick live playback latency diagnosis

## Executive summary

The requested three-attempt live YouTube Music diagnosis could not be completed. The first public internet attempt did not reach resolver completion or playback within more than two minutes, produced no playback trace record, and produced no result JSON. Per the execution limit, the investigation stopped and did not start attempts two or three. No live latency number is claimed.

The only supported conclusion is that the first attempt was blocked before an observable `resolver_completed` milestone. The evidence does not distinguish yt-dlp startup, Deno startup, YouTube response latency, or an external network/service stall. The current instrumentation cannot identify a sub-process stage while the resolver is still running.

## Environment and backend selection

- Branch: `feat/cpp-desktop-app`; existing uncommitted changes were preserved.
- Release executable: `/tmp/velune-phase2-release/velune_playback_benchmark`.
- Test command used: `--backend mpv --live-youtube-id dQw4w9WgXcQ --allow-live --scenario once --iterations 1`.
- Environment: `VELUNE_PLAYBACK_BACKEND=mpv`, `VELUNE_PLAYBACK_TRACE=1`, `LC_ALL=C`, staged libmpv 0.37 runtime and existing Phase 2 Qt/runtime environment.
- Backend policy: the benchmark requires the requested backend to be available and to report `backendName() == mpv`; it exits with status 3 instead of silently comparing a Qt fallback. The process remained active, so the command passed the immediate mpv availability gate. A prior local mpv smoke check with the same Release binary/runtime returned success, but it was synthetic and is not used as live evidence.
- No credentials or account cookies were supplied.

## Live attempts

Only one attempt was made, as required by the stop-on-blocker rule.

| Attempt | Source | Cache | Result | Observed latency |
| --- | --- | --- | --- | --- |
| 1 | public YouTube ID `dQw4w9WgXcQ` | unknown; resolver did not complete | resolver remained active beyond 125 s; terminated | unavailable |
| 2 | not run | — | stopped after blocker | — |
| 3 | not run | — | stopped after blocker | — |

Required breakdown for attempt 1:

```text
Track attempt: 1
Backend: libmpv (requested; benchmark rejects fallback)
Cache: unknown

Play request:          0 ms (trace record was not emitted)
Resolver complete:     unavailable
Relay ready:           unavailable
libmpv playable:       unavailable
Playback advancing:    unavailable
First PCM:             unavailable

Total observed latency: >125,000 ms until investigation termination
Primary bottleneck:    unresolved live resolver stage; component not established
```

The `>125,000 ms` value is an upper-bound observation to termination, not a measured startup latency. It must not be interpreted as time to first audio.

## Evidence and limitations

`PlaybackTrace` records monotonic milestones for play request, resolver start/completion/cache hit, relay readiness, backend load, media readiness, active state, and first positive position proxy. Production first PCM remains intentionally unavailable. No trace milestone was written before the blocked live attempt ended, so the available evidence is only that the live resolver path did not complete during the observation window.

The existing Phase 2 synthetic and injected-relay measurements cannot answer this live question: they exclude real YouTube, yt-dlp, Deno, CDN response time, and account/region behavior. No synthetic benchmark or full Phase 2 matrix was rerun for this report.

## Phase 2 audit

Phase 2 added the runtime-loaded libmpv C API backend, backend selection/fallback diagnostics, request/epoch fencing, async command handling, stale-event protection, deferred seeking, buffering/EOF/error handling, production tracing, the AudioResolver and AudioStream relay path, native/fake tests, benchmark harnesses, and Linux/Windows build/deployment definitions. Existing MusicService, library, Connect, embedded-session, and UI work was preserved.

The saved Phase 2 results demonstrate reliable local/relay fixture playback, stability sequences, PCM receipt on an isolated virtual sink, and a measured MP4 container advantage for mpv in the tested Linux/Qt environment. They do not demonstrate improved real YouTube startup latency. They also do not measure first audible speaker output. libmpv adds a runtime dependency and loader/ABI/resource packaging complexity; its lifecycle fencing and destruction changes improve reliability, not proven live startup time.

## Recommended next fixes (not implemented)

These are review candidates only; no performance fix was applied.

1. Add resolver subprocess phase diagnostics around `QProcess::start`, first stdout/stderr, and process finish, with bounded sanitized timing. Expected benefit: highest diagnostic value and likely ability to identify the >5 s stage. Risk: low; complexity: low.
2. Add an explicit resolver timeout/failure state visible in the UI and capture the helper exit code/category. Expected benefit: prevents an apparently frozen “Preparing audio” state. Risk: low to medium; complexity: low.
3. After the resolver stage is measured, optimize only the confirmed dominant stage (for example helper startup or upstream response). Expected benefit: potentially high but currently unknown. Risk: medium to high because extraction, cookies, regional behavior, and CDN policy can change; complexity: medium.

## Confirmation

No performance fixes, buffering changes, extraction changes, authentication changes, Android/TV changes, builds beyond existing binaries, or Phase 3 work were performed. No commit or push was made. The live diagnosis stopped after one blocked attempt; therefore it does not claim to have identified the exact root cause or successfully reproduced a complete live playback startup.
