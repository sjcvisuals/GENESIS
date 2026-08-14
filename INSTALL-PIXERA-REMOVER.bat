@echo off
REM ============================================================
REM  GENESIS - install ExtGenesisRemover into PIXERA data\rxExt
REM  Requests Administrator (Program Files). PIXERA must be quit.
REM ============================================================
cd /d "%~dp0"
echo Quit PIXERA fully before continuing.
echo This installer will request Administrator elevation.
pause
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-pixera-remover.ps1"
if errorlevel 1 (
  echo.
  echo Install failed.
  pause
  exit /b 1
)
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\check-pixera-dll.ps1" -DllPath "%ProgramFiles%\AV Stumpfl\Pixera\build_25-3_INTER_73\data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll"
pause
