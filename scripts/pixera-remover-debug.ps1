# Quick checklist after installing Remover v17 (FP16 Video In).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v17 - FP16 Video In format parity
=================================================
Root cause of v16 orange/input_black with LUID set:
  PIXERA Video In is R16G16B16A16_SFLOAT; v16 allocated RGBA8 (mem~8.8MB).
  v17 allocates matching FP16 (mem~16MB) and converts on download.

RenderBridge contract (ExtSampleD3D / ExtGenesis Background Remover):
  - Extension allocates shared GPU texture for Video In (is_target=false)
  - PIXERA writes into that share handle every frame
  - Extension reads via Vulkan (NOT host download_texture)
  - Extension publishes shared sampler (is_target=true)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v17-fp16-*)
5) Re-import ExtGenesisRemover.dll
6) On the Remover DEVICE, set Adapter LUID to the PIXERA engine GPU
7) Add Remover to timeline (cyan until Video In is linked is correct)
8) Link webcam/media to Video In
9) Expect: shared VIDEO_IN ... format=R16G16B16A16_SFLOAT mem~16M, then nz>0
10) Dump log if needed:

"@

& "$PSScriptRoot\dump-remover-log.ps1"
