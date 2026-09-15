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
  echo PREFLIGHT FAILED - do not install into PIXERA yet.
  pause
  exit /b %ERR%
)
echo PREFLIGHT PASSED. Next: INSTALL-PIXERA-REMOVER.bat (quit PIXERA first).
pause
exit /b 0
