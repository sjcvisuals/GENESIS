@echo off
REM ============================================================
REM  GENESIS - install ExtGenesisRemover into PIXERA data\rxExt
REM  Double-click this after BUILD-REMOVER.bat (or BUILD-AND-RUN.bat).
REM ============================================================
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-pixera-remover.ps1"
if errorlevel 1 (
  echo.
  echo Install failed.
  pause
  exit /b 1
)
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\check-pixera-dll.ps1"
pause
