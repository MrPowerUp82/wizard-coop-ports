#!/usr/bin/env bash
# Builds the PSP game into build-psp/ArcanaSurvivors/ (EBOOT.PBP + assets), ready to copy to
# ms0:/PSP/GAME/. Run from the repo root:
#   docker run --rm -v "$PWD":/src -w /src pspdev/pspdev bash tools/docker/psp-build.sh
set -euo pipefail
cmake -S platforms/psp/sdl -B build-psp -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-psp -j"$(nproc)"
GAME=build-psp/ArcanaSurvivors
rm -rf "$GAME"; mkdir -p "$GAME/assets/fonts"
cp build-psp/EBOOT.PBP "$GAME/"
cp assets/native_atlas_64.png assets/terrain_tiles_64.png "$GAME/assets/"
cp assets/fonts/DejaVuSans-Bold.ttf assets/fonts/DejaVu-LICENSE.txt "$GAME/assets/fonts/"
ls -la "$GAME" "$GAME/assets"
