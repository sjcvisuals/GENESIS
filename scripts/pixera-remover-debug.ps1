# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v35 - live blit
================================
Live GPU blit every frame (ExtSampleD3D). Matte Map is throttled on a private RT.
Never Map/CopyResource the shared textures PIXERA samples.

Expect:
  cyan     = waiting for Video In link
  live     = passthrough then matte
  STATUS   = stage=passthrough|matte  (changing mid=)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v35-live-blit-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; empty Video In OK; drag Live Input
7) Dump log - look for 'gpu_blit live', 'private_after_blit', no 'frozen_suspect'

"@

& "$PSScriptRoot\dump-remover-log.ps1"
