# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v38 - live + background matte
=============================================
PIXERA render thread: GPU blit only (v36 live path).
Background D3D device/thread: OpenSharedResource, blit, Map, RVM.
No Map/CopyResource/RVM on the PIXERA fence device.

Expect:
  live moving camera, then person cutout
  worker_capture nz>0, matte_ok from bg worker

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v38-bg-matte-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
