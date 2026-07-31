@echo off
REM ============================================================
REM  GENESIS — double-click this to build + run the editor (WIP)
REM  Also builds Pixera ExtGenesis + ExtGenesisRemover DLLs.
REM  Outputs land in the out\ folder next to this file.
REM ============================================================
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -Run
if errorlevel 1 (
  echo.
  echo Build failed. Scroll up for errors, or open build\last-build.log
  pause
)
