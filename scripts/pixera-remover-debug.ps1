# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v36 - blit + heartbeat
======================================
Blit-only (no Map/matte). Lime stripe diagnoses freeze:

  stripe MOVES + face frozen  => Video In stuck
  stripe ALSO frozen          => PIXERA not presenting output
  stripe MOVES + face live    => OK for matte next

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v36-blit-heartbeat-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Watch lime stripe; dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
