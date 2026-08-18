# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v40 - CPU RVM on capture-share
==============================================
PIXERA render thread: GPU blit Video In -> output (live) + blit into our capture share.
Background thread: Map capture share only, then RVM on ORT CPU (no DirectML / no Vulkan).
v39 froze the GPU at first DML/RVM load — lime heartbeat froze with the camera.

Expect:
  sustained live camera (moving lime heartbeat), then person cutout
  BgRemover ready ep=CPU, worker RVM process end, worker matte_ok

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v40-cpu-rvm-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
