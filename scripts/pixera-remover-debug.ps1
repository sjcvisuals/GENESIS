# Quick checklist after installing Remover v20 (full D3D11 I/O).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v20 - full D3D11 I/O (ExtSampleD3D parity)
=========================================================
v19 still orange with D3D11 Video In + fence but Vulkan output.
  ExtSampleD3D uses D3D11_IMAGE for BOTH Video In and sampler.

v20 contract:
  - Device: D3D11 device + D3D_FENCE timeline (unchanged)
  - Video In + Output sampler: both D3D11_IMAGE on that device
  - Matte: staging download -> RVM -> D3D output upload

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v20-d3d-io-*)
5) Re-import ExtGenesisRemover.dll
6) On the Remover DEVICE, set Adapter LUID to the PIXERA engine GPU
7) Add Remover to timeline (cyan until Video In is linked is correct)
8) Link webcam/media to Video In (ensure media is playing)
9) Expect: shared OUTPUT/VIDEO_IN handle=D3D11_IMAGE, then nz>0
10) Dump log if needed:

"@

& "$PSScriptRoot\dump-remover-log.ps1"
