@echo off
REM ============================================================
REM  GENESIS — standalone webcam BG-remover debug (no PIXERA)
REM  Double-click this from the repo folder.
REM  Builds if needed, then opens laptop camera + cutout window.
REM ============================================================
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-remover-camera-debug.ps1"
if errorlevel 1 (
  echo.
  echo Camera debug failed. Scroll up for errors.
  echo If build is missing, try: scripts\build.ps1 -CameraDebugOnly
  pause
)
