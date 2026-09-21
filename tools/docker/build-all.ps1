# Windows/PowerShell entry point for tools/docker/build-all.sh (runs it through Git Bash).
#   .\tools\docker\build-all.ps1               # every target
#   .\tools\docker\build-all.ps1 switch vita   # only some
$ErrorActionPreference = 'Stop'
$bash = @(
  "$env:ProgramFiles\Git\bin\bash.exe",
  "${env:ProgramFiles(x86)}\Git\bin\bash.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $bash) { throw 'Git Bash nao encontrado. Instale o Git for Windows ou rode build-all.sh em outro shell.' }
docker info *> $null
if ($LASTEXITCODE -ne 0) { throw 'Docker nao esta rodando. Abra o Docker Desktop e tente de novo.' }
$script = Join-Path $PSScriptRoot 'build-all.sh'
& $bash $script @args
exit $LASTEXITCODE
