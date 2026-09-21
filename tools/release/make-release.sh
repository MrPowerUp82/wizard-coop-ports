#!/usr/bin/env bash
# Builds every target in Docker from the current commit and prepares a GitHub release in
# dist/release/v<version>/: versioned artifacts, SHA256SUMS.txt and RELEASE_NOTES.md.
#
#   tools/release/make-release.sh            # version from platforms/switch/sdl/Makefile
#   tools/release/make-release.sh 0.5.1      # explicit version
#
# Refuses to run on a dirty tree so the hashes always correspond to a commit.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then export MSYS_NO_PATHCONV=1; MOUNT="$(pwd -W)"; else MOUNT="$ROOT"; fi

VERSION="${1:-$(sed -n 's/^APP_VERSION *:= *//p' platforms/switch/sdl/Makefile)}"
[[ -n "$VERSION" ]] || { echo "versão não encontrada"; exit 2; }
if [[ -n "$(git status --porcelain)" ]]; then
  echo "A árvore tem mudanças não commitadas; faça commit antes de gerar a release."; exit 2
fi
COMMIT="$(git rev-parse HEAD)"
SHORT="$(git rev-parse --short HEAD)"

bash tools/docker/build-all.sh host switch vita psp windows

OUT="dist/release/v$VERSION"
rm -rf "$OUT"; mkdir -p "$OUT"
NAME="arcana-survivors-v$VERSION"
cp dist/switch/arcana-survivors.nro "$OUT/$NAME-switch.nro"
cp dist/vita/arcana-survivors-native.vpk "$OUT/$NAME-vita.vpk"
cp dist/psp/arcana-survivors.iso "$OUT/$NAME-psp.iso"
cp dist/psp/arcana-survivors.cso "$OUT/$NAME-psp.cso"
# Linux bundle: executable + assets in the layout findAsset() expects.
STAGE="$(mktemp -d)"
mkdir -p "$STAGE/$NAME-linux-x86_64/assets/fonts"
cp dist/linux/arcana_desktop "$STAGE/$NAME-linux-x86_64/"
cp assets/native_atlas_128.png assets/terrain_tiles.png "$STAGE/$NAME-linux-x86_64/assets/"
cp assets/fonts/* "$STAGE/$NAME-linux-x86_64/assets/fonts/"
tar -C "$STAGE" --owner=0 --group=0 --mtime="@$(git log -1 --format=%ct)" -czf "$OUT/$NAME-linux-x86_64.tar.gz" "$NAME-linux-x86_64"
rm -rf "$STAGE"
# Windows bundle: exe + SDL2 DLLs + assets/, zipped inside the windows image with fixed timestamps.
# Staged under dist/ (not mktemp) so Docker can mount it from Git Bash on Windows too.
WIN_STAGE="dist/release/stage-windows"
rm -rf "$WIN_STAGE"; mkdir -p "$WIN_STAGE/$NAME-windows-x86_64"
cp -r dist/windows/. "$WIN_STAGE/$NAME-windows-x86_64/"
docker run --rm -v "$MOUNT":/src -w "/src/$WIN_STAGE" arcana-windows bash -c "find '$NAME-windows-x86_64' -exec touch -d @$(git log -1 --format=%ct) {} + && find '$NAME-windows-x86_64' | LC_ALL=C sort | TZ=UTC zip -X -q -@ '/src/$OUT/$NAME-windows-x86_64.zip'"
rm -rf "$WIN_STAGE"

(cd "$OUT" && sha256sum -- *.nro *.vpk *.iso *.cso *.tar.gz *.zip > SHA256SUMS.txt)

# Toolchain versions, for reproducibility.
GCC_HOST="$(docker run --rm arcana-host gcc -dumpfullversion)"
GCC_SWITCH="$(docker run --rm devkitpro/devkita64 bash -c '$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc -dumpversion')"
LIBNX="$(docker run --rm devkitpro/devkita64 bash -c "dkp-pacman -Q libnx switch-sdl2 | tr '\n' ' '")"
GCC_VITA="$(docker run --rm vitasdk/vitasdk bash -c 'arm-vita-eabi-gcc -dumpversion')"
GCC_PSP="$(docker run --rm pspdev/pspdev psp-gcc -dumpversion)"
SDL_VITA="$(docker run --rm vitasdk/vitasdk bash -c 'sed -n "s/^#define SDL_\(MAJOR\|MINOR\|PATCH\)LEVEL *//p;s/^#define SDL_\(MAJOR\|MINOR\)_VERSION *//p" $VITASDK/arm-vita-eabi/include/SDL2/SDL_version.h | paste -sd.')"
GCC_WIN="$(docker run --rm arcana-windows x86_64-w64-mingw32-g++ -dumpfullversion)"
SDL_WIN="$(docker run --rm arcana-windows bash -c 'sed -n "s/^#define SDL_\(MAJOR\|MINOR\|PATCH\)LEVEL *//p;s/^#define SDL_\(MAJOR\|MINOR\)_VERSION *//p" /opt/sdl-win64/include/SDL2/SDL_version.h | paste -sd.')"
image() { docker image inspect --format '{{index .RepoDigests 0}}' "$1" 2>/dev/null || echo "$1 (local)"; }

NOTES="$OUT/RELEASE_NOTES.md"
{
  sed "s/{{VERSION}}/$VERSION/g" tools/release/NOTES.md
  echo
  echo "## Arquivos"
  echo
  echo "| Arquivo | Tamanho | SHA-256 |"
  echo "|---|---|---|"
  while read -r hash file; do
    file="${file#\*}" # sha256sum on Windows marks binary mode with a leading "*"
    size=$(stat -c %s "$OUT/$file")
    printf '| `%s` | %s KB | `%s` |\n' "$file" "$(( (size + 1023) / 1024 ))" "$hash"
  done < "$OUT/SHA256SUMS.txt"
  echo
  echo "Para conferir: \`sha256sum -c SHA256SUMS.txt\` (Linux/macOS/Git Bash) ou \`Get-FileHash <arquivo> -Algorithm SHA256\` (PowerShell)."
  echo
  echo "## Build"
  echo
  echo "- Commit: \`$COMMIT\`"
  echo "- Gerado com \`tools/release/make-release.sh\` (Docker), em $(date -u +%Y-%m-%d)."
  echo "- Switch: devkitA64 GCC $GCC_SWITCH · $LIBNX· imagem \`$(image devkitpro/devkita64)\`"
  echo "- Vita: arm-vita-eabi GCC $GCC_VITA · SDL $SDL_VITA · imagem \`$(image vitasdk/vitasdk)\`"
  echo "- PSP: psp-gcc $GCC_PSP · simulação em float · imagem \`$(image pspdev/pspdev)\`"
  echo "- Windows: MinGW-w64 GCC $GCC_WIN (compilação cruzada) · SDL $SDL_WIN · imagem \`arcana-windows\`, \`tools/docker/windows.Dockerfile\`"
  echo "- Linux: GCC $GCC_HOST (imagem \`arcana-host\`, \`tools/docker/host.Dockerfile\`) · testes \`core\` e \`native_hotpath\` passando"
} > "$NOTES"

echo
echo "Release v$VERSION ($SHORT) pronta em $OUT:"
ls -la "$OUT"
echo
echo "Para publicar (GitHub CLI):"
echo "  git tag -a v$VERSION -m \"Arcana Survivors v$VERSION\" && git push origin v$VERSION"
echo "  gh release create v$VERSION $OUT/*.nro $OUT/*.vpk $OUT/*.cso $OUT/*.iso $OUT/*.tar.gz $OUT/*.zip $OUT/SHA256SUMS.txt --title \"Arcana Survivors v$VERSION\" --notes-file $NOTES"
