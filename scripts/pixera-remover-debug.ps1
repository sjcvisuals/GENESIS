# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v50 - auto GPU + live mask
==========================================
Your dual-GPU log should show ACTIVE GPU = NVIDIA (not AMD).
LUID 0 = auto (GPU with Windows displays, prefer NVIDIA).
Non-zero LUID = follow PIXERA injection (must match preview GPU).

Expect:
  stamp remover-timeline-v50-auto-gpu-*
  ACTIVE GPU ... NVIDIA GeForce ...
  ep=DML:0 (or DML:N)
  gpu_present live+mask

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = 0 (auto) or preview GPU hex; drag Live Input
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
