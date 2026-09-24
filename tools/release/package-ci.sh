#!/usr/bin/env bash
# Package the five independently built GitHub Actions artifacts for a tagged release.
set -euo pipefail

cd "$(dirname "$0")/../.."
tag="${1:?informe a tag da release}"
version="${tag#v}"
expected="$(sed -n 's/^APP_VERSION *:= *//p' platforms/switch/sdl/Makefile)"
[[ "$tag" == "v$expected" ]] || { echo "Tag $tag difere de APP_VERSION $expected" >&2; exit 2; }

out="dist/release/$tag"
name="arcana-survivors-$tag"
mkdir -p "$out" "dist/release/stage/$name-linux-x86_64/assets/fonts" "dist/release/stage/$name-windows-x86_64"

cp dist/switch/arcana-survivors.nro "$out/$name-switch.nro"
cp dist/vita/arcana-survivors-native.vpk "$out/$name-vita.vpk"
cp dist/psp/arcana-survivors.iso "$out/$name-psp.iso"
cp dist/psp/arcana-survivors.cso "$out/$name-psp.cso"

linux="dist/release/stage/$name-linux-x86_64"
cp dist/linux/arcana_desktop "$linux/"
cp assets/native_atlas_128.png assets/terrain_tiles.png assets/title_1280.png "$linux/assets/"
cp assets/fonts/* "$linux/assets/fonts/"
cp assets/cacert.pem "$linux/assets/"
cp THIRD_PARTY_NOTICES.txt "$linux/"
chmod +x "$linux/arcana_desktop"
commit_time="$(git log -1 --format=%ct)"
tar -C dist/release/stage --owner=0 --group=0 --mtime="@$commit_time" -czf "$out/$name-linux-x86_64.tar.gz" "$name-linux-x86_64"

windows="dist/release/stage/$name-windows-x86_64"
cp -r dist/windows/. "$windows/"
(cd dist/release/stage && find "$name-windows-x86_64" -exec touch -d "@$commit_time" {} + && find "$name-windows-x86_64" | LC_ALL=C sort | TZ=UTC zip -X -q -@ "../$tag/$name-windows-x86_64.zip")

(cd "$out" && sha256sum -- *.nro *.vpk *.iso *.cso *.tar.gz *.zip > SHA256SUMS.txt && sha256sum -c SHA256SUMS.txt)

{
  sed "s/{{VERSION}}/$version/g" tools/release/NOTES.md
  echo
  echo '## Arquivos'
  echo
  echo '| Arquivo | Tamanho | SHA-256 |'
  echo '|---|---:|---|'
  while read -r hash file; do
    size="$(stat -c %s "$out/$file")"
    printf '| `%s` | %s KB | `%s` |\n' "$file" "$(( (size + 1023) / 1024 ))" "$hash"
  done < "$out/SHA256SUMS.txt"
  echo
  echo "Build da tag \`$tag\` no commit \`$(git rev-parse HEAD)\`; testes do alvo Linux passaram antes do empacotamento."
} > "$out/RELEASE_NOTES.md"
