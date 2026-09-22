#!/usr/bin/env bash
# Cross-compiles the SDL2 desktop frontend for Windows x86_64 (MinGW-w64) inside the arcana-windows
# image and stages a ready-to-run bundle in build-windows/bundle: arcana-survivors.exe, the SDL2 DLLs and assets/.
# The tests run in the host (Linux) build; this one only produces the executable.
set -euo pipefail
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_FIND_ROOT_PATH=/opt/sdl-win64 -DCMAKE_PREFIX_PATH=/opt/sdl-win64 \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
  -DARCANA_BUILD_SERVER=OFF -DARCANA_BUILD_NET=OFF -DARCANA_BUILD_CLIENT=OFF \
  -DARCANA_BUILD_TESTS=OFF -DARCANA_BUILD_DESKTOP=ON -DARCANA_BUILD_ONLINE=ON >/dev/null
cmake --build build-windows --target arcana_desktop

BUNDLE=build-windows/bundle
rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/assets/fonts"
cp build-windows/arcana_desktop.exe "$BUNDLE/arcana-survivors.exe"
cp /opt/sdl-win64/bin/SDL2.dll /opt/sdl-win64/bin/SDL2_image.dll /opt/sdl-win64/bin/SDL2_ttf.dll "$BUNDLE/"
cp assets/native_atlas_128.png assets/terrain_tiles.png "$BUNDLE/assets/"
cp assets/fonts/* "$BUNDLE/assets/fonts/"
cp assets/cacert.pem "$BUNDLE/assets/"
x86_64-w64-mingw32-strip --strip-unneeded "$BUNDLE/arcana-survivors.exe"

# Anything besides Windows' own DLLs and the SDL2 ones next to the .exe would fail on a clean machine.
echo "DLLs importadas:"
x86_64-w64-mingw32-objdump -p "$BUNDLE/arcana-survivors.exe" | sed -n 's/^\s*DLL Name: //p' | sort -u | sed 's/^/  /'
