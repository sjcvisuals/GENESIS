# Quick checklist after installing Remover v29 (host-texture Video In).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v29 - host-texture
==================================
v18-v28 D3D11 shared Video In: fence/download OK but PIXERA never wrote the
feed (max_rgb=0). Restoring the host create/download/upload path that previously
got live pixels (BGRA), with abandon-timeout so scrub cannot hang forever.

Expect:
  cyan     = waiting for download_texture
  magenta  = download ok but empty
  live     = passthrough then matte
  STATUS   = stage=passthrough nz>0

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v31-shared-fp16-*)
5) Re-import ExtGenesisRemover.dll
6) Link Video In with media PLAYING, wait a few seconds (cooldown ~20 frames)
7) Dump log — look for 'video_in HOST', 'download armed', 'STATUS stage=passthrough'

"@

& "$PSScriptRoot\dump-remover-log.ps1"
