@echo off
rem Builds every target with Docker into dist\ (host tests, Linux, Switch .nro, Vita .vpk).
rem Usage: build.cmd            or   build.cmd switch vita
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\docker\build-all.ps1" %*
set CODE=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %CODE%
