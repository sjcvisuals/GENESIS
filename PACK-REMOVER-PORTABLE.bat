@echo off
REM Pack out\ExtGenesisRemover into a portable zip with INSTALL.bat
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\pack-remover-portable.ps1"
set ERR=%errorlevel%
if not "%ERR%"=="0" (
  echo PACK FAILED
  pause
  exit /b %ERR%
)
echo.
echo Output under out\GenesisRemover-Portable-*.zip
pause
exit /b 0
