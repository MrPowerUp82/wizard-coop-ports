#!/usr/bin/env bash
# Builds every target in Docker and collects the results in dist/.
#
#   tools/docker/build-all.sh                 # host tests + Linux desktop + Switch + Vita
#   tools/docker/build-all.sh switch vita     # only some targets (host | switch | vita)
#
# Needs only Docker. Images: arcana-host (built from tools/docker/host.Dockerfile),
# devkitpro/devkita64 and vitasdk/vitasdk (pulled on first use).
# Works from Linux, macOS and Git Bash on Windows (tools/docker/build-all.ps1 wraps it for PowerShell).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# Git Bash on Windows: stop MSYS from rewriting container paths, and give Docker a Windows path.
MOUNT="$ROOT"
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
  export MSYS_NO_PATHCONV=1
  MOUNT="$(pwd -W)"
fi

TARGETS=("$@")
[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=(host switch vita)

run() { # image, script
  docker run --rm -v "$MOUNT":/src -w /src "$1" bash "$2"
}

mkdir -p dist
declare -A RESULT
for target in "${TARGETS[@]}"; do
  echo "==> $target"
  case "$target" in
    host)
      docker build -q -t arcana-host -f tools/docker/host.Dockerfile tools/docker >/dev/null
      if run arcana-host tools/docker/host-build.sh; then
        mkdir -p dist/linux
        cp build-linux/arcana_desktop dist/linux/
        cp assets/native_atlas_128.png assets/terrain_tiles.png dist/linux/
        mkdir -p dist/linux/fonts && cp assets/fonts/* dist/linux/fonts/
        RESULT[$target]="ok  dist/linux/arcana_desktop (testes passaram)"
      else RESULT[$target]="FALHOU"; fi
      ;;
    switch)
      if run devkitpro/devkita64 tools/docker/switch-build.sh; then
        mkdir -p dist/switch && cp platforms/switch/sdl/arcana-survivors.nro dist/switch/
        RESULT[$target]="ok  dist/switch/arcana-survivors.nro"
      else RESULT[$target]="FALHOU"; fi
      ;;
    vita)
      if run vitasdk/vitasdk tools/docker/vita-build.sh; then
        mkdir -p dist/vita && cp build-vita/arcana-survivors-native.vpk dist/vita/
        RESULT[$target]="ok  dist/vita/arcana-survivors-native.vpk"
      else RESULT[$target]="FALHOU"; fi
      ;;
    *) echo "alvo desconhecido: $target (use host, switch ou vita)"; exit 2 ;;
  esac
done

echo
echo "Resumo:"
failed=0
for target in "${TARGETS[@]}"; do
  printf '  %-7s %s\n' "$target" "${RESULT[$target]}"
  [[ "${RESULT[$target]}" == FALHOU ]] && failed=1
done
exit $failed
