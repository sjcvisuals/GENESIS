# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v52 - custom quality + camera delay
===================================================
Quality: Fast / Balanced / Quality presets, plus Custom.
  Custom Downsample (0.08-1.0) and Custom Infer Side (192-1280)
  only apply when Quality=Custom. Capture stays 640-wide.

Camera Delay (ms): delays the displayed live feed so it can line
  up with the lagged matte. Matte path is unchanged.
  Start around host-log cycle_ms (often 60-100ms).

Expect:
  stamp remover-timeline-v52-custom-quality-delay-*
  host log: quality -> Custom downsample=... max_side=...
  matte_ok ... q=Custom(ds=...,side=...)
  DLL log: camera_delay_ms=N when delay > 0

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Live Input; try Quality=Custom and Camera Delay
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
