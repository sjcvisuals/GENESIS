@echo off
REM ============================================================
REM  GENESIS - install ExtGenesisRemover into PIXERA data\rxExt
REM  Right-click this file -> "Run as administrator"
REM  PIXERA must be fully quit first.
REM ============================================================
cd /d "%~dp0"

net session >nul 2>&1
if not %errorlevel%==0 (
  echo Requesting Administrator...
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%~dp0scripts\install-pixera-remover.ps1\"'"
  echo.
  echo If install failed, open:
  echo   %TEMP%\genesis-install-remover.log
  pause
  exit /b %errorlevel%
)

echo Running elevated install...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-pixera-remover.ps1"
set ERR=%errorlevel%
echo.
echo Install log: %TEMP%\genesis-install-remover.log
if not "%ERR%"=="0" (
  echo INSTALL FAILED - open the log above.
  if exist "%TEMP%\genesis-install-remover.log" type "%TEMP%\genesis-install-remover.log"
  pause
  exit /b %ERR%
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\check-pixera-dll.ps1" -DllPath "%ProgramFiles%\AV Stumpfl\Pixera\build_25-3_INTER_73\data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll"
echo.
echo Running installed health check...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\health-check-remover.ps1" -InstalledOnly
pause
exit /b 0
