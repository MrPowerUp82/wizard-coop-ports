#!/usr/bin/env bash
# Packs the PSP build into a UMD image: build-psp/arcana-survivors.iso and .cso (one file each,
# playable on CFW, Vita/Adrenaline and PPSSPP). Needs build-psp/ from psp-build.sh. Run from the
# repo root in the host image (genisoimage + python3):
#   docker run --rm -v "$PWD":/src -w /src arcana-host bash tools/docker/psp-iso.sh
set -euo pipefail
UMD=build-psp/umd
rm -rf "$UMD"; mkdir -p "$UMD/PSP_GAME/SYSDIR" "$UMD/PSP_GAME/USRDIR/assets/fonts"
cp build-psp/PARAM_UMD.SFO "$UMD/PSP_GAME/PARAM.SFO"
cp platforms/psp/sdl/icon0.png "$UMD/PSP_GAME/ICON0.PNG"
cp platforms/psp/sdl/pic1.png "$UMD/PSP_GAME/PIC1.PNG"
# Unencrypted PRX: accepted by CFW, Adrenaline and PPSSPP (retail firmware never ran homebrew).
cp build-psp/arcana_psp.prx "$UMD/PSP_GAME/SYSDIR/EBOOT.BIN"
cp assets/native_atlas_64.png assets/native_animations_32_*.png assets/terrain_tiles_64.png assets/title_480.png "$UMD/PSP_GAME/USRDIR/assets/"
cp assets/fonts/DejaVuSans-Bold.ttf assets/fonts/DejaVu-LICENSE.txt "$UMD/PSP_GAME/USRDIR/assets/fonts/"
printf 'ARCA-90001|0000000000000001|0001|G' > "$UMD/UMD_DATA.BIN"
# Fixed timestamps so the image only changes when its content does.
find "$UMD" -exec touch -d "@${SOURCE_DATE_EPOCH:-0}" {} +
genisoimage -quiet -iso-level 4 -xa -A "PSP GAME" -V "ARCANA" -sysid "PSP GAME" -volset "ARCANA" \
  -p "" -publisher "" -o build-psp/arcana-survivors.iso "$UMD"
python3 tools/psp/make_cso.py build-psp/arcana-survivors.iso build-psp/arcana-survivors.cso
ls -la build-psp/arcana-survivors.iso build-psp/arcana-survivors.cso
