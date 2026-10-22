@echo off
rem hil-win.cmd - double-click to start the automated hardware run (hil-win.ps1) in this desktop session
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0hil-win.ps1" %*
echo.
pause
