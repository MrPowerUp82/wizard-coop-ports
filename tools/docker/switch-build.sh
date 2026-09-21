#!/usr/bin/env bash
# Builds platforms/switch/sdl/arcana-survivors.nro. Run from the repo root:
#   docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 bash tools/docker/switch-build.sh
set -euo pipefail
cd platforms/switch/sdl
mkdir -p romfs
cp ../../../assets/native_atlas_128.png romfs/
make -j"$(nproc)"
ls -la arcana-survivors.nro
