@echo off
rem Build Pinol from source and open it, on Windows: double-click, or run "build.cmd [options]" (see build.ps1)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set status=%errorlevel%
rem Opened with a double-click: keep the window to read what went wrong
if not "%status%"=="0" if not defined CI pause
exit /b %status%
