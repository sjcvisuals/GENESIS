# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v55 - map-stall cooldown
========================================
Root cause of sticky 2-3fps: matte host Copy+Map of the shared capture
stalls the same GPU PIXERA uses. Under outputs Map gets slow; keeping
kicking keeps preview stalled after outputs are off.

v55:
  - host publishes map_ms
  - if map_ms stays high, cooldown (no kicks ~2s) so live+last-matte
    can run at full rate and the GPU recovers
  - after repeated cooldowns, relaunch matte host
  - stressed mode: 320-wide capture, slower kicks

Expect:
  stamp remover-timeline-v55-map-cooldown-*
  host: matte_ok ... map_ms=...
  dll: matte_cooldown ... when outputs thrash the GPU

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Live Input OK -> enable outputs -> disable outputs (preview should recover)
7) Dump log — check map_ms and matte_cooldown lines

'@

& "$PSScriptRoot\dump-remover-log.ps1"
