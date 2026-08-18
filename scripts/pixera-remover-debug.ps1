# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v43 - live blit only (no ORT in PIXERA)
=======================================================
In-process ONNX Runtime freezes PIXERA (v39-v42).
This build is the proven v36 live path: GPU blit + lime heartbeat.
Person cutout: use .\scripts\run-remover-camera-debug.ps1 outside PIXERA.
Timeline cutout needs out-of-process matte (next).

Expect:
  continuously moving camera + lime stripe
  NO MatteCaptureWorker / BgRemover / ORT log lines

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v43-live-blit-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
