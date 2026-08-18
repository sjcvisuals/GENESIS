# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v42 - preload CPU RVM (no Vulkan)
=================================================
Worker preloads ORT CPU before any capture kick.
Capture share is released before RVM infer.
PIXERA DLL no longer links RemoverPipeline/Vulkan.

Expect:
  'preload OK' before first kick
  continuously moving lime
  worker RVM process end + matte_ok

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v42-preload-cpu-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
