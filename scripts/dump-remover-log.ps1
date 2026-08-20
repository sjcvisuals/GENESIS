# Dump ExtGenesisRemover + MatteHost logs (ASCII-only; safe for Windows PowerShell 5.1)
param(
    [int]$Tail = 80
)

$ErrorActionPreference = 'Continue'
$log = Join-Path $env:TEMP 'ExtGenesisRemover.log'
$hostLog = Join-Path $env:TEMP 'GenesisRemoverMatteHost.log'

Write-Host '=== ExtGenesisRemover.log ==='
Write-Host ('path: ' + $log)
if (Test-Path -LiteralPath $log) {
    Get-Item -LiteralPath $log | Format-List FullName, Length, LastWriteTime
    Write-Host ('--- last ' + $Tail + ' lines ---')
    Get-Content -LiteralPath $log -Tail $Tail
} else {
    Write-Host 'MISSING - extension has not written a log yet.'
    Write-Host 'Quit PIXERA, rebuild/install, re-import Remover, set Adapter LUID, then Live Input again.'
}

Write-Host ''
Write-Host '=== GenesisRemoverMatteHost.log ==='
Write-Host ('path: ' + $hostLog)
if (Test-Path -LiteralPath $hostLog) {
    Get-Item -LiteralPath $hostLog | Format-List FullName, Length, LastWriteTime
    Write-Host ('--- last ' + $Tail + ' lines ---')
    Get-Content -LiteralPath $hostLog -Tail $Tail
} else {
    Write-Host 'MISSING - matte host has not started (or CREATE_NO_WINDOW host failed to launch).'
}

Write-Host ''
Write-Host 'Expect stamp remover-timeline-v47-oop-orient-...'
Write-Host 'DLL: applied host matte, stage=matte, not stuck busy=1'
Write-Host 'Host: ep=DML (or CPU), matte_ok, infer_ms, cycle_ms'
Write-Host 'Adapter LUID = PREVIEW engine GPU when testing preview.'
