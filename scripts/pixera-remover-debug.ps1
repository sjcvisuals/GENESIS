# Quick checklist after installing Remover v18 (D3D11 Video In).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v18 - D3D11_IMAGE Video In (ExtSampleD3D)
=========================================================
v17 still orange: Vulkan OPAQUE_WIN32 Video In stayed black even with FP16
  (mem still ~8.8MB). PIXERA's D3D engine writes D3D11 shared textures.

v18 contract:
  - Video In  = D3D11_IMAGE (ExtSampleD3D SHARED|SHARED_NTHANDLE)
  - Output    = Vulkan OPAQUE_WIN32 sampler (unchanged)
  - Matte     = D3D staging download -> RVM -> upload Vulkan out

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v18-d3d11vin-*)
5) Re-import ExtGenesisRemover.dll
6) On the Remover DEVICE, set Adapter LUID to the PIXERA engine GPU
7) Add Remover to timeline (cyan until Video In is linked is correct)
8) Link webcam/media to Video In
9) Expect: shared VIDEO_IN ... handle=D3D11_IMAGE, then nz>0 / passthrough / matte
10) Dump log if needed:

"@

& "$PSScriptRoot\dump-remover-log.ps1"
