# Quick checklist after installing Remover v25 (GPU blit + sentinel).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v25 - gpublit
=============================
v24: download OK (dl=1) but nz=0 orange forever.

v25:
  - GPU blit Video In → output (like ExtSampleD3D)
  - CPU matte reads the blitted RGBA8 output
  - 1x1 mid magenta sentinel
  - On black, keep GPU blit visible (look at the timeline!)

What you should see / log:
  Live video on clip  → PIXERA writes; matte should follow if nz>0
  Magenta speck/mid   → SENTINEL STILL PRESENT (PIXERA not writing)
  Black + zeros      → PIXERA wrote black (check media link/playing)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v25-gpublit-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID + Video In with media PLAYING
7) Dump log + note what the timeline LOOKS like

"@

& "$PSScriptRoot\dump-remover-log.ps1"
