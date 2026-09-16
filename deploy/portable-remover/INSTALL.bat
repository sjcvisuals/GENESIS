@echo off
REM ============================================================
REM  GENESIS Remover - portable install into any PIXERA root
REM  Right-click -> Run as administrator
REM  PIXERA must be fully quit first.
REM ============================================================
cd /d "%~dp0"

echo.
echo GENESIS Remover portable installer
echo Package folder: %~dp0
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
set ERR=%errorlevel%
echo.
echo Install log: %TEMP%\genesis-install-remover-portable.log
if not "%ERR%"=="0" (
  echo INSTALL FAILED - open the log above.
  if exist "%TEMP%\genesis-install-remover-portable.log" type "%TEMP%\genesis-install-remover-portable.log"
  pause
  exit /b %ERR%
)
echo.
echo Done. In PIXERA, browse to data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll
pause
exit /b 0
