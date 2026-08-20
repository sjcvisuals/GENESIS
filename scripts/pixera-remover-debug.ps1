# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v49 - live camera + matte alpha mask
====================================================
v48 confirmed GPU: ep=DML:0, infer_ms ~12-18.
Lag was from displaying the whole cutout at matte rate (~15fps).

v49: each PIXERA present samples LIVE Video In, gated by latest matte alpha.
Camera motion stays smooth; silhouette updates at matte_fps.

Expect:
  stamp remover-timeline-v49-live-mask-*
  ep=DML:N
  preview feels live (not stepped)

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
