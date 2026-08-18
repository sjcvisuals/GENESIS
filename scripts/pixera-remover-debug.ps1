# Quick checklist after installing Remover v24 (blocking Map after fence).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v24 - blockingmap
=================================
v23 kept STATUS alive (good!) but download_fail forever:
  fence OK → CopyResource → DO_NOT_WAIT Map → always WAS_STILL_DRAWING.

v24: blocking Map after fence Wait completes (waits only for our Copy).

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v24-blockingmap-*)
5) Re-import ExtGenesisRemover.dll
6) Set Adapter LUID, link Video In (media playing)
7) Dump log - want nz>0 / passthrough|matte|input_black (not endless download_fail)

"@

& "$PSScriptRoot\dump-remover-log.ps1"
