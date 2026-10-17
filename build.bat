@echo off
setlocal

set "CONFIGURATION=%~1"
if not defined CONFIGURATION set "CONFIGURATION=Release"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-All.ps1" -Configuration "%CONFIGURATION%"
exit /b %ERRORLEVEL%
