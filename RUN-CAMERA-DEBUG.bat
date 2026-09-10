@echo off
REM ============================================================
REM  GENESIS - standalone webcam BG-remover debug (no PIXERA)
REM  Always rebuilds the camera debug target so HUD/FPS updates land.
REM ============================================================
cd /d "%~dp0"
echo Forcing camera-debug rebuild (so FPS HUD is present)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-remover-camera-debug.ps1" -Build
if errorlevel 1 (
  echo.
  echo Camera debug failed. Scroll up for errors.
  pause
)
