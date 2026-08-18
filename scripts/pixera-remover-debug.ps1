# Quick checklist after installing Remover v26 (v24 CPU path + Video In RTV).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v26 - cpu-rtv
=============================
v25 hung black again right after VIDEO_IN (GPU blit path).

v26:
  - Same CPU download as v24 (STATUS must keep printing)
  - Video In allocated with SRV|RTV (PIXERA may need RTV to write)
  - No GPU write/Flush before publish
  - FP16 raw mid bits when pixels stay zero

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v26-cpu-rtv-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID + Video In with media PLAYING
7) Dump log — need continuous STATUS after VIDEO_IN + any FP16 raw mid= lines

"@

& "$PSScriptRoot\dump-remover-log.ps1"
