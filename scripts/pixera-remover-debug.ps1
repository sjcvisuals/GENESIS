# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v37 - live + async matte
========================================
Live GPU blit every frame (v36 proven). Matte via async private capture
(non-blocking poll/Map) so the fence handshake never stalls.

Expect:
  live passthrough first, then person cutout
  STATUS stage=matte, async_capture nz>0, matte_ok

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v37-async-matte-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
