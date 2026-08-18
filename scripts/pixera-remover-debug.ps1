# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v41 - gentle single-share capture
=================================================
PIXERA render: live Video In blit + lime heartbeat (never Flush).
Worker: opens ONE capture share once, Maps slowly, CPU RVM.
v40 OpenSharedResource storm + Flush froze PIXERA ~5s.

Expect:
  continuously moving camera + lime stripe
  one 'opened CAPTURE share', worker_capture nz>0, ep=CPU, matte_ok

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v41-gentle-capture-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
