# Quick checklist after installing Remover v22 (no Device::render fence Wait).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v22 - nofencewait (black hang fix)
==================================================
v21 log showed fence sync working, then silence after VIDEO_IN allocate.
Cause: Device::render GPU Wait + Input CopyResource/Map deadlock → solid black.

v22 removes that Wait. STATUS must keep printing after VIDEO_IN.

STATUS extras:
  fence=COMPLETED/COUNTER  - after_render Signal progress (Wait removed)
  sync=N                   - Device before/render/after call count
  sent=1                   - magenta sentinel still in Video In (PIXERA not writing)
  sent=0 + nz=0            - PIXERA overwrote with black (source/link issue)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v22-nofencewait-*)
5) Re-import ExtGenesisRemover.dll
6) Set Adapter LUID, link Video In (media playing)
7) Dump log - STATUS must continue after shared VIDEO_IN

"@

& "$PSScriptRoot\dump-remover-log.ps1"
