#!/usr/bin/env bash
# Builds the PSP game into build-psp/ArcanaSurvivors/ (EBOOT.PBP + assets), ready to copy to
# ms0:/PSP/GAME/, plus the UMD PARAM.SFO used by psp-iso.sh. Run from the repo root:
#   docker run --rm -v "$PWD":/src -w /src pspdev/pspdev bash tools/docker/psp-build.sh
set -euo pipefail
cmake -S platforms/psp/sdl -B build-psp -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-psp -j"$(nproc)"
# PARAM.SFO for the UMD image (category UG = disc game); the EBOOT folder keeps its own MG one.
mksfoex -s CATEGORY=UG -s DISC_ID=ARCA90001 -s DISC_VERSION=1.00 -s APP_VER=00.80 -s PSP_SYSTEM_VER=6.60 \
  -d BOOTABLE=1 -d DISC_NUMBER=1 -d DISC_TOTAL=1 -d PARENTAL_LEVEL=1 -d REGION=32768 -d MEMSIZE=1 \
  "Arcana Survivors" build-psp/PARAM_UMD.SFO
GAME=build-psp/ArcanaSurvivors
rm -rf "$GAME"; mkdir -p "$GAME/assets/fonts"
cp build-psp/EBOOT.PBP "$GAME/"
cp assets/native_atlas_64.png assets/terrain_tiles_64.png "$GAME/assets/"
cp assets/fonts/DejaVuSans-Bold.ttf assets/fonts/DejaVu-LICENSE.txt "$GAME/assets/fonts/"
ls -la "$GAME" "$GAME/assets"
