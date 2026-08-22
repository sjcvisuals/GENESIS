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
if (Test-Path -LiteralPath $log) {
    Write-Host ''
    Write-Host 'DLL auto-pick / LUID lines:'
    Select-String -LiteralPath $log -Pattern 'pickAdapter|AUTO adapter_luid|using PIXERA/injected LUID|Device::initialize' |
        Select-Object -Last 20 |
        ForEach-Object { Write-Host $_.Line }
}
Write-Host ''
Write-Host 'Note: C++ extensions cannot read PIXERA output assignments. LUID 0 = auto'
Write-Host '(first GPU with Windows displays, prefer NVIDIA). Non-zero LUID = follow PIXERA.'
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
Write-Host 'Expect stamp remover-timeline-v52-custom-quality-delay-...'
Write-Host 'Quality Custom: set Custom Downsample + Custom Infer Side.'
Write-Host 'Camera Delay (ms): delay live feed to match matte lag (try cycle_ms from host log).'
Write-Host 'Adapter LUID = 0 for auto, or NVIDIA luid_hex to force. Same GPU as PIXERA preview.'
