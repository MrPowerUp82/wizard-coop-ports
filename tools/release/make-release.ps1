# Builds every target in Docker from the current commit and prepares a GitHub release in
# dist\release\v<version>\: versioned artifacts, SHA256SUMS.txt and RELEASE_NOTES.md.
# Native PowerShell (5.1 or 7); only Docker Desktop and Git are needed.
#
#   .\tools\release\make-release.ps1          # version from platforms\switch\sdl\Makefile
#   .\tools\release\make-release.ps1 0.6.1    # explicit version
#
# Or double-click release.cmd in the repository root. Refuses to run on a dirty tree so the
# hashes always correspond to a commit.
param([string]$Version)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $Root
$Utf8 = New-Object System.Text.UTF8Encoding($false)

if (-not $Version) {
  $match = Select-String -Path platforms\switch\sdl\Makefile -Pattern '^APP_VERSION\s*:=\s*(\S+)'
  if (-not $match) { throw 'versao nao encontrada no Makefile do Switch' }
  $Version = $match.Matches[0].Groups[1].Value
}
if (git status --porcelain) {
  Write-Host 'A arvore tem mudancas nao commitadas; faca commit antes de gerar a release.' -ForegroundColor Red
  exit 2
}
$Commit = (git rev-parse HEAD).Trim()
$Short = (git rev-parse --short HEAD).Trim()
$CommitTime = (git log -1 --format=%ct).Trim()

& (Join-Path $Root 'tools\docker\build-all.ps1') host switch vita
if ($LASTEXITCODE -ne 0) { Write-Host 'Build falhou; release nao gerada.' -ForegroundColor Red; exit 1 }

$Out = "dist\release\v$Version"
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
$Name = "arcana-survivors-v$Version"
Copy-Item dist\switch\arcana-survivors.nro "$Out\$Name-switch.nro"
Copy-Item dist\vita\arcana-survivors-native.vpk "$Out\$Name-vita.vpk"

# Linux bundle, packed inside the host image so owners/permissions/timestamps are reproducible.
$Stage = "dist\release\stage\$Name-linux-x86_64"
if (Test-Path dist\release\stage) { Remove-Item -Recurse -Force dist\release\stage }
New-Item -ItemType Directory -Force "$Stage\assets\fonts" | Out-Null
Copy-Item dist\linux\arcana_desktop $Stage\
Copy-Item assets\native_atlas_128.png, assets\terrain_tiles.png "$Stage\assets\"
Copy-Item assets\fonts\* "$Stage\assets\fonts\"
docker run --rm -v "${Root}:/src" -w /src arcana-host bash -c "chmod +x '$($Stage -replace '\\','/')/arcana_desktop' && tar -C dist/release/stage --owner=0 --group=0 --mtime=@$CommitTime -czf '$($Out -replace '\\','/')/$Name-linux-x86_64.tar.gz' '$Name-linux-x86_64'"
if ($LASTEXITCODE -ne 0) { throw 'falha ao empacotar o build Linux' }
Remove-Item -Recurse -Force dist\release\stage

# SHA-256 through .NET: Get-FileHash lives in a module that Windows PowerShell 5.1 fails to load
# when launched from PowerShell 7 (inherited PSModulePath).
function Get-Sha256([string]$Path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [IO.File]::OpenRead($Path)
  try { return ([BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLower() }
  finally { $stream.Dispose(); $sha.Dispose() }
}

# SHA256SUMS.txt in the coreutils format, so `sha256sum -c` works on Linux/macOS too.
$files = Get-ChildItem $Out -File | Where-Object { $_.Extension -in '.nro', '.vpk', '.gz' } | Sort-Object Name
$sums = foreach ($f in $files) { '{0}  {1}' -f (Get-Sha256 $f.FullName), $f.Name }
[IO.File]::WriteAllText((Join-Path $Root "$Out\SHA256SUMS.txt"), (($sums -join "`n") + "`n"), $Utf8)

# Toolchain versions, for reproducibility.
function Get-FromImage([string]$Image, [string]$Command) { ((docker run --rm $Image bash -c $Command) -join ' ').Trim() }
function Get-Digest([string]$Image) {
  $d = (docker image inspect --format '{{index .RepoDigests 0}}' $Image 2>$null)
  if ($LASTEXITCODE -ne 0 -or -not $d) { return "$Image (local)" }
  return $d.Trim()
}
$gccHost = Get-FromImage 'arcana-host' 'gcc -dumpfullversion'
$gccSwitch = Get-FromImage 'devkitpro/devkita64' '$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc -dumpversion'
$libnx = Get-FromImage 'devkitpro/devkita64' 'dkp-pacman -Q libnx switch-sdl2'
$gccVita = Get-FromImage 'vitasdk/vitasdk' 'arm-vita-eabi-gcc -dumpversion'
# No double quotes here: Windows PowerShell 5.1 mangles them when calling native programs.
$sdlVita = Get-FromImage 'vitasdk/vitasdk' 'grep -E SDL_MAJOR_VERSION\|SDL_MINOR_VERSION\|SDL_PATCHLEVEL $VITASDK/arm-vita-eabi/include/SDL2/SDL_version.h | grep -oE [0-9]+$ | head -n3 | paste -sd.'

$notes = New-Object System.Collections.Generic.List[string]
$notes.Add(([IO.File]::ReadAllText((Join-Path $Root 'tools\release\NOTES.md'), $Utf8)).Replace('{{VERSION}}', $Version).TrimEnd())
$notes.Add('')
$notes.Add('## Arquivos')
$notes.Add('')
$notes.Add('| Arquivo | Tamanho | SHA-256 |')
$notes.Add('|---|---|---|')
foreach ($f in $files) {
  $hash = Get-Sha256 $f.FullName
  $notes.Add(('| `{0}` | {1} KB | `{2}` |' -f $f.Name, [math]::Ceiling($f.Length / 1024), $hash))
}
$notes.Add('')
$notes.Add('Para conferir: `sha256sum -c SHA256SUMS.txt` (Linux/macOS) ou `Get-FileHash <arquivo> -Algorithm SHA256` (PowerShell).')
$notes.Add('')
$notes.Add('## Build')
$notes.Add('')
$notes.Add("- Commit: ``$Commit``")
$notes.Add("- Gerado com ``tools/release/make-release.ps1`` (Docker), em $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd')).")
$notes.Add("- Switch: devkitA64 GCC $gccSwitch · $libnx · imagem ``$(Get-Digest 'devkitpro/devkita64')``")
$notes.Add("- Vita: arm-vita-eabi GCC $gccVita · SDL $sdlVita · imagem ``$(Get-Digest 'vitasdk/vitasdk')``")
$notes.Add("- Linux: GCC $gccHost (imagem ``arcana-host``, ``tools/docker/host.Dockerfile``) · testes passando")
$NotesPath = Join-Path $Root "$Out\RELEASE_NOTES.md"
[IO.File]::WriteAllText($NotesPath, (($notes -join "`n") + "`n"), $Utf8)

Write-Host ''
Write-Host "Release v$Version ($Short) pronta em $Out" -ForegroundColor Green
Get-ChildItem $Out | Format-Table Name, Length -AutoSize | Out-String | Write-Host
Write-Host 'Para publicar (GitHub CLI):'
Write-Host "  git push origin main"
Write-Host "  git tag -a v$Version -m `"Arcana Survivors v$Version`""
Write-Host "  git push origin v$Version"
Write-Host "  gh release create v$Version $Out\$Name-switch.nro $Out\$Name-vita.vpk $Out\$Name-linux-x86_64.tar.gz $Out\SHA256SUMS.txt --title `"Arcana Survivors v$Version`" --notes-file $Out\RELEASE_NOTES.md"
Write-Host ''
Write-Host "Ou pelo site: crie a release na tag v$Version, cole RELEASE_NOTES.md e anexe os 4 arquivos."
