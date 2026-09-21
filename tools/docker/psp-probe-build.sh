#!/usr/bin/env bash
# Builds the PSP CPU probe EBOOT.PBP. Run from the repo root:
#   docker run --rm -v "$PWD":/src -w /src pspdev/pspdev bash tools/docker/psp-probe-build.sh
set -euo pipefail
cmake -S platforms/psp/probe -B build-psp-probe -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-psp-probe -j"$(nproc)"
ls -la build-psp-probe/EBOOT.PBP
