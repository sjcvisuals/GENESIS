@echo off
REM Headless Remover validation BEFORE PIXERA.
REM Builds (unless -SkipBuild), runs matting smoke + host --smoke + package health.
cd /d "%~dp0"
echo === GENESIS Remover PREFLIGHT (no PIXERA) ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\preflight-remover.ps1" %*
set ERR=%ERRORLEVEL%
echo.
echo Report: %TEMP%\ExtGenesisRemover.preflight.txt
if not "%ERR%"=="0" (
  echo PREFLIGHT FAILED - package/smoke not ready. Fix report, then retry.
  pause
  exit /b %ERR%
)
echo PREFLIGHT PASSED - package is ready.
echo Next: quit PIXERA, then INSTALL-PIXERA-REMOVER.bat
echo Then: HEALTH-CHECK-REMOVER.bat (confirms installed stamp is v63b)
pause
exit /b 0
