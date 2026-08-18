# Quick checklist after installing Remover v21 (sentinel + fence diagnostics).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v21 - sentinel + fence diagnostics
==================================================
v20 still orange with full D3D11 I/O. This build diagnoses WHY:

STATUS extras:
  fence=COMPLETED/COUNTER  - does PIXERA signal our D3D_FENCE?
  sync=N                   - Device before/render/after call count (0 = never synced)
  sent=1                   - magenta sentinel still in Video In (PIXERA not writing)
  sent=0 + nz=0            - PIXERA overwrote with black (source/link issue)

Also: Video In now allows RTV bind; CPU fence wait before staging read.

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v21-sentinel-*)
5) Re-import ExtGenesisRemover.dll
6) Set Adapter LUID, link Video In (media playing)
7) Dump log and send the STATUS lines with fence=/sync=/sent=

"@

& "$PSScriptRoot\dump-remover-log.ps1"
