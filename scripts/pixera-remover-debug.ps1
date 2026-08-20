# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v45 - faster out-of-process matte
=================================================
PIXERA DLL: live blit, no ORT, no console host window.
MatteHost: DML preferred, kicks ASAP when idle, keeps capture open.

Expect:
  no blank console window
  smoother cutout FPS
  host log shows ep=DML (or CPU) and infer_ms

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v45-oop-fast-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
