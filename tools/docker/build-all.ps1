# Builds every target in Docker and collects the results in dist\. Native PowerShell (5.1 or 7):
# only Docker Desktop is needed, no Git Bash/WSL.
#
#   .\tools\docker\build-all.ps1               # host tests + Linux desktop + Switch + Vita
#   .\tools\docker\build-all.ps1 switch vita   # only some targets (host | switch | vita | windows | psp | psp-probe)
#
# Or double-click build.cmd in the repository root.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $Root

docker info *> $null
if ($LASTEXITCODE -ne 0) { Write-Host 'Docker nao esta rodando. Abra o Docker Desktop e tente de novo.' -ForegroundColor Red; exit 1 }
if (-not $Targets -or $Targets.Count -eq 0) { $Targets = @('host', 'switch', 'vita') }

function Invoke-Container([string]$Image, [string]$Script) {
  # The scripts run inside Linux containers; the repo keeps them LF-only (.gitattributes).
  # Out-Host: the build log must not leak into the return value (a non-empty array is always truthy).
  docker run --rm -v "${Root}:/src" -w /src $Image bash $Script | Out-Host
  return ($LASTEXITCODE -eq 0)
}

New-Item -ItemType Directory -Force dist | Out-Null
$results = [ordered]@{}
foreach ($target in $Targets) {
  Write-Host "==> $target" -ForegroundColor Cyan
  switch ($target) {
    'host' {
      docker build -q -t arcana-host -f tools/docker/host.Dockerfile tools/docker | Out-Null
      if (Invoke-Container 'arcana-host' 'tools/docker/host-build.sh') {
        New-Item -ItemType Directory -Force dist\linux\fonts | Out-Null
        New-Item -ItemType Directory -Force dist\linux\assets | Out-Null
        Copy-Item build-linux\arcana_desktop dist\linux\ -Force
        Copy-Item assets\native_atlas_128.png, assets\terrain_tiles.png, assets\title_1280.png dist\linux\ -Force
        Copy-Item assets\cacert.pem dist\linux\assets\ -Force
        Copy-Item assets\fonts\* dist\linux\fonts\ -Force
        $results[$target] = 'ok  dist\linux\arcana_desktop (testes passaram)'
      } else { $results[$target] = 'FALHOU' }
    }
    'switch' {
      if (Invoke-Container 'devkitpro/devkita64' 'tools/docker/switch-build.sh') {
        New-Item -ItemType Directory -Force dist\switch | Out-Null
        Copy-Item platforms\switch\sdl\arcana-survivors.nro dist\switch\ -Force
        $results[$target] = 'ok  dist\switch\arcana-survivors.nro'
      } else { $results[$target] = 'FALHOU' }
    }
    'vita' {
      if (Invoke-Container 'vitasdk/vitasdk' 'tools/docker/vita-build.sh') {
        New-Item -ItemType Directory -Force dist\vita | Out-Null
        Copy-Item build-vita\arcana-survivors-native.vpk dist\vita\ -Force
        $results[$target] = 'ok  dist\vita\arcana-survivors-native.vpk'
      } else { $results[$target] = 'FALHOU' }
    }
    'windows' {
      docker build -q -t arcana-windows -f tools/docker/windows.Dockerfile tools/docker | Out-Null
      if (Invoke-Container 'arcana-windows' 'tools/docker/windows-build.sh') {
        if (Test-Path dist\windows) { Remove-Item -Recurse -Force dist\windows }
        New-Item -ItemType Directory -Force dist\windows | Out-Null
        Copy-Item -Recurse build-windows\bundle\* dist\windows\
        $results[$target] = 'ok  dist\windows\arcana-survivors.exe (+ DLLs do SDL2 e assets\)'
      } else { $results[$target] = 'FALHOU' }
    }
    'psp' {
      docker build -q -t arcana-host -f tools/docker/host.Dockerfile tools/docker | Out-Null
      if ((Invoke-Container 'pspdev/pspdev' 'tools/docker/psp-build.sh') -and (Invoke-Container 'arcana-host' 'tools/docker/psp-iso.sh')) {
        if (Test-Path dist\psp) { Remove-Item -Recurse -Force dist\psp }
        New-Item -ItemType Directory -Force dist\psp | Out-Null
        Copy-Item -Recurse build-psp\ArcanaSurvivors dist\psp\
        Copy-Item build-psp\arcana-survivors.iso, build-psp\arcana-survivors.cso dist\psp\
        $results[$target] = 'ok  dist\psp\arcana-survivors.iso / .cso + pasta ArcanaSurvivors\'
      } else { $results[$target] = 'FALHOU' }
    }
    'psp-probe' {
      if (Invoke-Container 'pspdev/pspdev' 'tools/docker/psp-probe-build.sh') {
        New-Item -ItemType Directory -Force dist\psp-probe | Out-Null
        Copy-Item build-psp-probe\EBOOT.PBP dist\psp-probe\ -Force
        $results[$target] = 'ok  dist\psp-probe\EBOOT.PBP'
      } else { $results[$target] = 'FALHOU' }
    }
    default { Write-Host "alvo desconhecido: $target (use host, switch, vita, windows, psp ou psp-probe)" -ForegroundColor Red; exit 2 }
  }
}

Write-Host ''
Write-Host 'Resumo:'
$failed = $false
foreach ($key in $results.Keys) {
  $color = 'Green'
  if ($results[$key] -eq 'FALHOU') { $color = 'Red'; $failed = $true }
  Write-Host ('  {0,-7} {1}' -f $key, $results[$key]) -ForegroundColor $color
}
if ($failed) { exit 1 }
exit 0
