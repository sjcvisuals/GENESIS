# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v39 - capture-share matte
=========================================
PIXERA render thread: GPU blit Video In -> output (live) + blit into our capture share.
Background D3D device/thread: OpenSharedResource on capture share only, Map, RVM.
Never opens PIXERA Video In (v38 freeze). Live present until worker matte_ok.

Expect:
  sustained live camera (lime heartbeat), then person cutout
  opened CAPTURE share, worker_capture nz>0, worker matte_ok

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v39-capture-share-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
