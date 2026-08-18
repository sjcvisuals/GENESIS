# Quick checklist after installing Remover v23 (gated Map after fence Wait).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v23 - mapgate (no Map hang)
===========================================
v22 still went silent after VIDEO_IN. Likely Map while fence Wait pending
or Map racing PIXERA writes.

v23:
  - Restore ExtSampleD3D Device::render Wait
  - Map ONLY after waitCpu succeeds (else blue + STATUS, never hang)
  - Map uses DO_NOT_WAIT
  - ShareHandle = type+handle only (like ExtSampleD3D)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v23-mapgate-*)
5) Re-import ExtGenesisRemover.dll
6) Set Adapter LUID, link Video In (media playing)
7) Dump log - must see STATUS or 'render vin-ready enter' after VIDEO_IN

"@

& "$PSScriptRoot\dump-remover-log.ps1"
