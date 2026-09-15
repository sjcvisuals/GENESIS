@echo off
REM Build ExtGenesisRemover then run PREFLIGHT (headless gates before PIXERA).
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -RemoverOnly
if errorlevel 1 (
  echo Remover build failed. See build\last-build.log
  pause
  exit /b 1
)
echo.
echo Running PREFLIGHT (skip rebuild)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\preflight-remover.ps1" -SkipBuild
set ERR=%errorlevel%
if not "%ERR%"=="0" (
  echo PREFLIGHT FAILED - do not install into PIXERA yet.
  echo Report: %TEMP%\ExtGenesisRemover.preflight.txt
  pause
  exit /b %ERR%
)
echo.
echo PREFLIGHT PASSED. Next: INSTALL-PIXERA-REMOVER.bat (quit PIXERA first).
pause
exit /b 0
