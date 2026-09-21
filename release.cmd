@echo off
rem Builds everything with Docker and prepares dist\release\v<version>\ (artifacts, SHA256SUMS, notes).
rem Usage: release.cmd          or   release.cmd 0.6.1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\release\make-release.ps1" %*
set CODE=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %CODE%
