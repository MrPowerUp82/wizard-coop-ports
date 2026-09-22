#!/usr/bin/env bash
# Builds core + tests + SDL2 desktop frontend inside the arcana-host image and runs the tests.
set -euo pipefail
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DARCANA_BUILD_SERVER=OFF -DARCANA_BUILD_NET=OFF -DARCANA_BUILD_CLIENT=OFF \
  -DARCANA_BUILD_TESTS=ON -DARCANA_BUILD_DESKTOP=ON -DARCANA_BUILD_ONLINE=ON >/dev/null
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
# On Windows hosts the bind-mounted NTFS volume drops the exec bit that the linker set, which a
# fresh `docker run` container (as used by online-e2e.sh) then sees as non-executable; restore it
# here, inside the image that owns the build.
chmod +x build-linux/arcana_desktop
