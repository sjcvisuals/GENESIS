# Dump ExtGenesisRemover + MatteHost logs (ASCII-only; safe for Windows PowerShell 5.1)
param(
    [int]$Tail = 80
)

$ErrorActionPreference = 'Continue'
$log = Join-Path $env:TEMP 'ExtGenesisRemover.log'
$hostLog = Join-Path $env:TEMP 'GenesisRemoverMatteHost.log'

Write-Host '=== Which GPU is Remover using? ==='
if (Test-Path -LiteralPath $hostLog) {
    $gpuLines = Select-String -LiteralPath $hostLog -Pattern 'DXGI adapter\[|ACTIVE GPU|NOTE: Remover|preferred DML|BgRemover ready ep=' |
        Select-Object -Last 40
    if ($gpuLines) {
        $gpuLines | ForEach-Object { Write-Host $_.Line }
    } else {
        Write-Host 'No GPU lines yet - start Live Input once, then re-run this script.'
    }
} else {
    Write-Host ('MISSING host log: ' + $hostLog)
}
Write-Host ''
Write-Host 'To use NVIDIA: set PIXERA preview engine + Remover Adapter LUID to the NVIDIA luid_hex above.'
Write-Host 'Shared textures require Remover and PIXERA on the SAME GPU.'
Write-Host ''

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
Write-Host 'Expect stamp remover-timeline-v49-live-mask-...'
Write-Host 'DLL: stage=matte, ep=DML:N - preview should feel live (camera motion at present rate).'
Write-Host 'Host: infer_ms ~10-20, matte_fps (alpha update rate; silhouette may be ~15fps).'
Write-Host 'Adapter LUID = PREVIEW engine GPU when testing preview.'
