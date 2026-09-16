# Dump ExtGenesisRemover + MatteHost logs (ASCII-only; safe for Windows PowerShell 5.1)
param(
    [int]$Tail = 80
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$log = Join-Path $env:TEMP 'ExtGenesisRemover.log'
$hostLog = Join-Path $env:TEMP 'GenesisRemoverMatteHost.log'
$failFile = Join-Path $env:TEMP 'ExtGenesisRemover.FAIL.txt'
$healthReport = Join-Path $env:TEMP 'ExtGenesisRemover.health.txt'

$expectedStamp = 'remover-demo-v63-debug-...'
$buildStampCpp = Join-Path $RepoRoot 'extensions\ExtGenesisRemover\src\BuildStamp.cpp'
if (Test-Path -LiteralPath $buildStampCpp) {
    $ipcText = Get-Content -LiteralPath $buildStampCpp -Raw -ErrorAction SilentlyContinue
    if ($ipcText -match 'kBuildStamp\[\]\s*=\s*"([^"]+)"') {
        $expectedStamp = $Matches[1].Trim()
    }
}

Write-Host '=== FAIL / health files ==='
if (Test-Path -LiteralPath $failFile) {
    Write-Host ('FAIL FILE PRESENT: ' + $failFile) -ForegroundColor Red
    Get-Content -LiteralPath $failFile
} else {
    Write-Host 'No ExtGenesisRemover.FAIL.txt (good)'
}
if (Test-Path -LiteralPath $healthReport) {
    Write-Host ('Last health report: ' + $healthReport)
    Get-Content -LiteralPath $healthReport -Tail 25
} else {
    Write-Host 'No health report yet - run HEALTH-CHECK-REMOVER.bat'
}

Write-Host ''
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
    Write-Host ''
    Write-Host '--- recent HEALTH / STATUS ---'
    Select-String -LiteralPath $log -Pattern 'HEALTH |STATUS |FAIL |STAMP_MISMATCH' |
        Select-Object -Last 20 |
        ForEach-Object { Write-Host $_.Line }
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
    Write-Host ''
    Write-Host '--- recent HEALTH / IDENTITY / capture ---'
    Select-String -LiteralPath $hostLog -Pattern 'HEALTH |IDENTITY |capture_from_shm|STAMP_MISMATCH|matte_ok' |
        Select-Object -Last 20 |
        ForEach-Object { Write-Host $_.Line }
} else {
    Write-Host 'MISSING - matte host has not started (or CREATE_NO_WINDOW host failed to launch).'
}

Write-Host ''
Write-Host ('Expect stamp: ' + $expectedStamp)
Write-Host 'v63: debug harness (HEALTH/FAIL/shm). v62 was CPU capture.'
Write-Host 'Look for HEALTH / capture_from_shm / shm=1 / map_ms= / infer_ms=.'
Write-Host 'Adapter LUID = 0 for auto, or NVIDIA luid_hex to force. Same GPU as PIXERA preview.'
Write-Host 'Debug Status should show: stamp + map_ms + infer_ms + ep + shm=1 + matte=1'
Write-Host 'Quick triage: .\HEALTH-CHECK-REMOVER.bat'
