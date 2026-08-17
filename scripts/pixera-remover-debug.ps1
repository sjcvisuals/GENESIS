# Quick checklist after installing Remover v19 (D3D11 + Device fence).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v19 - D3D11_IMAGE + Device D3D_FENCE (ExtSampleD3D)
===================================================================
v18 still orange: textures were D3D11_IMAGE but Device had no timeline fence.
  ExtSampleD3D publishes D3D_FENCE SyncDesc so PIXERA can write Video In.

v19 contract:
  - Device owns one D3D11 device + shared ID3D11Fence (TimelineSemaphore)
  - Video In  = D3D11_IMAGE on that same device
  - Output    = Vulkan OPAQUE_WIN32 sampler
  - Matte     = after fence wait -> staging download -> RVM -> Vulkan out

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v19-d3d-fence-*)
5) Re-import ExtGenesisRemover.dll
6) On the Remover DEVICE, set Adapter LUID to the PIXERA engine GPU
7) Add Remover to timeline (cyan until Video In is linked is correct)
8) Link webcam/media to Video In (ensure media is playing)
9) Expect: SharedD3d11 ready ... fence, VIDEO_IN D3D11_IMAGE, then nz>0
10) Dump log if needed:

"@

& "$PSScriptRoot\dump-remover-log.ps1"
