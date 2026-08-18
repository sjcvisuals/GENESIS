# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v44 - live blit + out-of-process matte
=====================================================
PIXERA DLL: live Video In blit (no ORT).
GenesisRemoverMatteHost.exe: loads ORT, Maps capture share, returns cutout via IPC.

Expect:
  moving lime while host loads
  host_ready=1, MatteHostClient kick, applied host matte
  person cutout (stage=matte)

Also check %TEMP%\GenesisRemoverMatteHost.log

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v44-oop-matte-*)
5) Re-import ExtGenesisRemover.dll (MatteHost.exe must sit next to DLL)
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"

$hostLog = Join-Path $env:TEMP "GenesisRemoverMatteHost.log"
Write-Host ""
Write-Host "==== matte host log (tail) ====" -ForegroundColor Cyan
if (Test-Path $hostLog) {
    Get-Content $hostLog -Tail 30
} else {
    Write-Host "MISSING $hostLog — host did not start (DLL launch failed or exe missing)."
}
