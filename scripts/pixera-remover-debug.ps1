# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v53 - output-load throttle + recover
====================================================
Preview OK but outputs drop to 2-3fps (and stay slow after):
  matte host Map/readback was stalling the shared GPU. v53:
  - wall-clock kick throttle (~10fps max, backs off with host cycle)
  - pause kicks + drop delay ring if present_ms stays >280ms
  - relaunch matte host after repeated collapses
  - leaner camera-delay ring (fewer HD buffers)

Also still has v52 Custom quality + Camera Delay (ms).

Expect:
  stamp remover-timeline-v53-output-throttle-*
  gpu_present ... present_ms=.. min_kick_ms=.. host_cycle_ms=..
  gpu_recover pause kicks ... if overload detected

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Live Input; enable outputs; watch FPS; disable outputs - should recover
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
