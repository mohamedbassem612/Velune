#!/usr/bin/env bash
# Development-only isolated PulseAudio server; never changes the desktop's audio route.
set -euo pipefail
command -v pulseaudio >/dev/null
command -v pactl >/dev/null
if [[ $# == 0 ]]; then
 printf 'Usage: %s <benchmark-or-matrix-command> [arguments...]\n' "$0" >&2
 exit 2
fi
audio_directory=$(mktemp -d "${TMPDIR:-/tmp}/velune-benchmark-audio.XXXXXX")
audio_pid=
cleanup() {
 if [[ -n "$audio_pid" ]]; then kill "$audio_pid" 2>/dev/null || true; wait "$audio_pid" 2>/dev/null || true; fi
 rm -rf -- "$audio_directory"
}
trap cleanup EXIT INT TERM
export PULSE_RUNTIME_PATH="$audio_directory"
export PULSE_SERVER="unix:$audio_directory/native"
export PULSE_SINK=velune_benchmark
# Do not register a second audio server on the desktop session bus.
export DBUS_SESSION_BUS_ADDRESS="unix:path=$audio_directory/no-session-bus"
pulseaudio -n --daemonize=no --use-pid-file=no --exit-idle-time=-1 --disable-shm --disallow-exit \
 --load="module-native-protocol-unix socket=$audio_directory/native auth-anonymous=1" \
 --load='module-null-sink sink_name=velune_benchmark rate=48000 channels=2 norewinds=1' \
 >"$audio_directory/server.log" 2>&1 &
audio_pid=$!
ready=false
for attempt in {1..100}; do
 if pactl info >/dev/null 2>&1; then ready=true; break; fi
 if ! kill -0 "$audio_pid" 2>/dev/null; then break; fi
 sleep .05
done
if [[ "$ready" != true ]]; then cat "$audio_directory/server.log" >&2; exit 1; fi
"$@"
