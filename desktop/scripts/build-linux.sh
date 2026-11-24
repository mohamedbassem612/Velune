#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cmake -S "$root" -B "$root/build-qml" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build-qml" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
