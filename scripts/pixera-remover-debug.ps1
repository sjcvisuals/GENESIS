# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v54 - kick throttle fix
=======================================
v53 made both preview and output laggy: it paced kicks from host
cycle_ms (includes idle until next kick) which ratcheted slower,
and mistook Input::render gaps for low FPS then paused kicks.

v54:
  - pace from infer_ms only (+ small floor ~33ms)
  - no present_ms kick pauses / no auto host restart
  - still one-in-flight (busy gate) so Map stalls cannot stack

Expect:
  stamp remover-timeline-v54-kick-throttle-fix-*
  gpu_present ... min_kick_ms=~33-40 infer_ms=~12-20

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Live Input; preview should feel snappy again; then try outputs
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
