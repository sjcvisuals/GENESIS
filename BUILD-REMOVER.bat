@echo off
REM ============================================================
REM  GENESIS - build ExtGenesisRemover only (Release) + sync out\
REM  Double-click this. Bypasses PowerShell execution policy.
REM ============================================================
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -RemoverOnly
if errorlevel 1 (
  echo.
  echo Remover build failed. See build\last-build.log
  pause
  exit /b 1
)
echo.
echo Running package health check...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\health-check-remover.ps1"
echo.
echo Next: double-click INSTALL-PIXERA-REMOVER.bat
echo Then: double-click HEALTH-CHECK-REMOVER.bat
pause
