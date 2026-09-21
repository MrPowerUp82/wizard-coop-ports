#!/usr/bin/env bash
# Builds the PS Vita .vpk. Run from the repo root:
#   docker run --rm -v "$PWD":/src -w /src vitasdk/vitasdk bash tools/docker/vita-build.sh
set -euo pipefail
cmake -S platforms/vita/sdl -B build-vita -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-vita -j"$(nproc)"
ls -la build-vita/*.vpk
