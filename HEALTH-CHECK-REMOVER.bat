@echo off
REM ============================================================
REM  GENESIS - one-shot ExtGenesisRemover health check
REM  Run after BUILD-REMOVER.bat / INSTALL-PIXERA-REMOVER.bat
REM ============================================================
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\health-check-remover.ps1" %*
set ERR=%errorlevel%
echo.
echo Full report: %TEMP%\ExtGenesisRemover.health.txt
if not "%ERR%"=="0" (
  echo HEALTH CHECK FAILED
  pause
  exit /b %ERR%
)
echo HEALTH CHECK OK
pause
exit /b 0
