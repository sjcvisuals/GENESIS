# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v51 - quality toggle fix
========================================
Fast/Balanced/Quality now change RVM presets in the matte host.
Capture stays 640-wide (realloc on quality change was killing cutout).

Expect:
  stamp remover-timeline-v51-quality-fix-*
  host log: quality -> Fast|Balanced|Quality when you toggle
  matte_ok ... q=Balanced (or Quality)
  cutout stays up in all three modes

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Live Input; toggle Quality Fast/Balanced/Quality
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
