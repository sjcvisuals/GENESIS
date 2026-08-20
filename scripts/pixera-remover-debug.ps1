# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v47 - upright + faster OOP matte
================================================
v46 cutout worked but was upside-down (~8fps on 1080p Map).

v47:
  capture blit without V-flip (matte was flipped twice)
  capture at max 960 wide (cheaper Map+RVM)
  adaptive host settle (50ms then 12ms)
  dump script uses ASCII single-quoted strings only

Expect:
  stamp remover-timeline-v47-oop-orient-*
  upright cutout
  host matte_ok with size=960x540 (or similar) and ~fps=
  dump-remover-log.ps1 runs without string terminator errors

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
