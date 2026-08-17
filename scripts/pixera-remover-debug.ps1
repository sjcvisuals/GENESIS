# Quick checklist after installing Remover v16 (ExtGenesis parity).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v16 - ExtGenesis texture parity
===============================================
RenderBridge contract (ExtSampleD3D / ExtGenesis Background Remover):
  - Extension allocates shared GPU texture for Video In (is_target=false)
  - PIXERA writes into that share handle every frame
  - Extension reads via Vulkan (NOT host download_texture)
  - Extension publishes shared sampler (is_target=true)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v16-parity-*)
5) Re-import ExtGenesisRemover.dll
6) On the Remover DEVICE, set Adapter LUID to the PIXERA engine GPU
   (if it is 0000000000000000, orange/black is expected)
7) Add Remover to timeline (cyan until Video In is linked is correct)
8) Link webcam/media to Video In
9) Dump log if needed:

"@

& "$PSScriptRoot\dump-remover-log.ps1"
