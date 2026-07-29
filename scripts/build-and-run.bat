@echo off
REM Double-click this file (or run from cmd) to build GENESIS and open the editor.
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Run
if errorlevel 1 pause
