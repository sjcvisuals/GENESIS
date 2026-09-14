# One-shot ExtGenesisRemover health check (ASCII-only for Windows PowerShell 5.1).
# Run after BUILD-REMOVER.bat and/or INSTALL-PIXERA-REMOVER.bat.
#
# Usage:
#   .\scripts\health-check-remover.ps1
#   .\scripts\health-check-remover.ps1 -InstalledOnly
#   .\scripts\health-check-remover.ps1 -RequireUnlocked
#   .\HEALTH-CHECK-REMOVER.bat

param(
    [string]$PixeraRoot = "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73",
    [string]$PackageDir = "",
    [switch]$InstalledOnly,
    [switch]$RequireUnlocked,
    [switch]$Quiet
)

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot

# Prefer stamp from MatteIpc.h (single source of truth); fall back if unreadable.
$MatteIpcHeader = Join-Path $RepoRoot "extensions\ExtGenesisRemover\src\MatteIpc.h"
$ExpectedStamp = "remover-demo-v63-debug-20260914"
if (Test-Path -LiteralPath $MatteIpcHeader) {
    $hdrText = Get-Content -LiteralPath $MatteIpcHeader -Raw -ErrorAction SilentlyContinue
    if ($hdrText -match 'kBuildStamp\[\]\s*=\s*"([^"]+)"') {
        $ExpectedStamp = $Matches[1].Trim()
    }
}
if ($ExpectedStamp -match '^(remover-demo-v\d+)') {
    $ExpectedStampPrefix = $Matches[1]
} else {
    $ExpectedStampPrefix = "remover-demo-v63"
}

if (-not $PackageDir) {
    $PackageDir = Join-Path $RepoRoot "out\ExtGenesisRemover"
}
$InstalledDir = Join-Path $PixeraRoot "data\rxExt\ExtGenesisRemover"
$DllLog = Join-Path $env:TEMP "ExtGenesisRemover.log"
$HostLog = Join-Path $env:TEMP "GenesisRemoverMatteHost.log"
$FailFile = Join-Path $env:TEMP "ExtGenesisRemover.FAIL.txt"
$ReportPath = Join-Path $env:TEMP "ExtGenesisRemover.health.txt"

$script:PassCount = 0
$script:FailCount = 0
$script:WarnCount = 0
$script:Lines = New-Object System.Collections.Generic.List[string]

function Write-Report([string]$Message, [string]$Level = "INFO") {
    $line = "[{0}] {1}" -f $Level, $Message
    [void]$script:Lines.Add($line)
    if ($Quiet -and $Level -eq "INFO") { return }
    $color = switch ($Level) {
        "PASS" { "Green" }
        "FAIL" { "Red" }
        "WARN" { "Yellow" }
        default { "Gray" }
    }
    Write-Host $line -ForegroundColor $color
}

function Add-Pass([string]$Message) {
    $script:PassCount++
    Write-Report $Message "PASS"
}
function Add-Fail([string]$Message) {
    $script:FailCount++
    Write-Report $Message "FAIL"
}
function Add-Warn([string]$Message) {
    $script:WarnCount++
    Write-Report $Message "WARN"
}

function Get-StampFromFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
    if (-not $raw) { return $null }
    if ($raw -match 'build=([^\r\n]+)') { return $Matches[1].Trim() }
    if ($raw -match '(remover-demo-v\d+[^\r\n\s]*)') { return $Matches[1].Trim() }
    return $null
}

function Test-OnnxBlob([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Add-Fail ("{0} missing: {1}" -f $Label, $Path)
        return
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -lt 1000000) {
        $head = ""
        try { $head = (Get-Content -LiteralPath $Path -TotalCount 2) -join " " } catch {}
        if ($head -match 'git-lfs') {
            Add-Fail ("{0} is a Git LFS pointer ({1:N0} bytes). Run: git lfs pull" -f $Label, $item.Length)
        } else {
            Add-Fail ("{0} too small ({1:N0} bytes) - expected real ONNX >= 1 MB" -f $Label, $item.Length)
        }
        return
    }
    Add-Pass ("{0} OK ({1:N1} MB)" -f $Label, ($item.Length / 1MB))
}

function Test-Package([string]$Dir, [string]$Label, [switch]$MissingIsWarn) {
    Write-Report ("--- Package: {0} ({1}) ---" -f $Label, $Dir) "INFO"
    if (-not (Test-Path -LiteralPath $Dir)) {
        if ($MissingIsWarn) {
            Add-Warn ("{0} folder missing (not installed yet?): {1}" -f $Label, $Dir)
        } else {
            Add-Fail ("{0} folder missing: {1}" -f $Label, $Dir)
        }
        return
    }

    $dll = Join-Path $Dir "ExtGenesisRemover.dll"
    $hostExe = Join-Path $Dir "GenesisRemoverMatteHost.exe"
    $ort = Join-Path $Dir "onnxruntime.dll"
    $rvm = Join-Path $Dir "assets\matting\rvm_mobilenetv3_fp16.onnx"
    $stampFile = Join-Path $Dir "BUILD_STAMP.txt"

    foreach ($pair in @(
            @{ Path = $dll; Name = "ExtGenesisRemover.dll" },
            @{ Path = $hostExe; Name = "GenesisRemoverMatteHost.exe" },
            @{ Path = $ort; Name = "onnxruntime.dll" }
        )) {
        if (Test-Path -LiteralPath $pair.Path) {
            $it = Get-Item -LiteralPath $pair.Path
            Add-Pass ("{0}/{1} present ({2:N0} bytes, {3})" -f $Label, $pair.Name, $it.Length, $it.LastWriteTime)
        } else {
            Add-Fail ("{0}/{1} MISSING" -f $Label, $pair.Name)
        }
    }

    Test-OnnxBlob $rvm ("{0}/RVM model" -f $Label)

    $stamp = Get-StampFromFile $stampFile
    if (-not $stamp) {
        Add-Fail ("{0}/BUILD_STAMP.txt missing or unreadable" -f $Label)
    } elseif ($stamp -notmatch [regex]::Escape($ExpectedStampPrefix)) {
        Add-Fail ("{0} stamp '{1}' does not match expected prefix {2}" -f $Label, $stamp, $ExpectedStampPrefix)
    } elseif ($stamp -ne $ExpectedStamp) {
        Add-Warn ("{0} stamp '{1}' (expected exact '{2}')" -f $Label, $stamp, $ExpectedStamp)
    } else {
        Add-Pass ("{0} stamp OK: {1}" -f $Label, $stamp)
    }

    if ((Test-Path -LiteralPath $dll) -and $RequireUnlocked) {
        try {
            $fs = [System.IO.File]::Open($dll, 'Open', 'ReadWrite', 'None')
            $fs.Close()
            Add-Pass ("{0} DLL not locked" -f $Label)
        } catch {
            Add-Fail ("{0} DLL locked (quit PIXERA): {1}" -f $Label, $_.Exception.Message)
        }
    }
}

function Test-PixeraRunning {
    $names = @("Pixera", "PixeraDesigner", "PixeraEngine", "PixeraManager")
    $running = @()
    foreach ($n in $names) {
        $running += @(Get-Process -Name $n -ErrorAction SilentlyContinue)
    }
    if ($running.Count -gt 0) {
        $msg = ($running | ForEach-Object { "PID $($_.Id) $($_.ProcessName)" }) -join "; "
        if ($RequireUnlocked) {
            Add-Fail ("PIXERA still running: {0}" -f $msg)
        } else {
            Add-Warn ("PIXERA running (OK for live check; quit before install): {0}" -f $msg)
        }
    } else {
        Add-Pass "PIXERA not running"
    }
}

function Test-Logs {
    Write-Report "--- Runtime logs ---" "INFO"
    if (Test-Path -LiteralPath $FailFile) {
        $failText = (Get-Content -LiteralPath $FailFile -Raw -ErrorAction SilentlyContinue)
        Add-Fail ("FAIL file present: {0} => {1}" -f $FailFile, (($failText -replace '\s+', ' ').Trim()))
    } else {
        Add-Pass "No ExtGenesisRemover.FAIL.txt (good)"
    }

    foreach ($pair in @(
            @{ Path = $DllLog; Name = "DLL log" },
            @{ Path = $HostLog; Name = "Host log" }
        )) {
        if (-not (Test-Path -LiteralPath $pair.Path)) {
            Add-Warn ("{0} missing yet: {1} (load Remover in PIXERA once)" -f $pair.Name, $pair.Path)
            continue
        }
        $it = Get-Item -LiteralPath $pair.Path
        $ageMin = [int]((Get-Date) - $it.LastWriteTime).TotalMinutes
        if ($ageMin -gt 120) {
            Add-Warn ("{0} stale ({1} min old): {2}" -f $pair.Name, $ageMin, $pair.Path)
        } else {
            Add-Pass ("{0} fresh ({1} min old, {2:N0} bytes)" -f $pair.Name, $ageMin, $it.Length)
        }

        $tail = @(Get-Content -LiteralPath $pair.Path -Tail 120 -ErrorAction SilentlyContinue)
        $joined = ($tail -join "`n")
        if ($joined -match [regex]::Escape($ExpectedStampPrefix)) {
            Add-Pass ("{0} contains {1}" -f $pair.Name, $ExpectedStampPrefix)
        } elseif ($joined -match 'remover-demo-v6[012]') {
            Add-Fail ("{0} still shows old stamp (v60/v61/v62) - reinstall v63 package" -f $pair.Name)
        } else {
            Add-Warn ("{0} has no recognisable stamp in last 120 lines" -f $pair.Name)
        }

        foreach ($bad in @('STAMP_MISMATCH', 'FATAL: RVM', 'Git LFS', 'capture_nz0', 'map_failed')) {
            $hits = @($tail | Select-String -SimpleMatch $bad)
            if ($hits.Count -gt 0) {
                Add-Warn ("{0} contains '{1}' (x{2})" -f $pair.Name, $bad, $hits.Count)
            }
        }
        if ($joined -match 'capture_from_shm|capture_path=shm|HEALTH .*shm=1|matte_kick_cpu') {
            Add-Pass ("{0} shows CPU/shm capture path" -f $pair.Name)
        }
        if ($joined -match 'legacy_gpu_map|map_skip_reuse') {
            Add-Warn ("{0} may still be on legacy GPU Map path" -f $pair.Name)
        }
    }
}

function Test-PackageVsInstalled {
    if (-not (Test-Path -LiteralPath $PackageDir)) { return }
    if (-not (Test-Path -LiteralPath $InstalledDir)) { return }
    $outDll = Join-Path $PackageDir "ExtGenesisRemover.dll"
    $insDll = Join-Path $InstalledDir "ExtGenesisRemover.dll"
    if (-not ((Test-Path -LiteralPath $outDll) -and (Test-Path -LiteralPath $insDll))) { return }
    $o = Get-Item -LiteralPath $outDll
    $i = Get-Item -LiteralPath $insDll
    if ($i.Length -ne $o.Length) {
        Add-Fail ("Installed DLL size {0:N0} != package {1:N0} - reinstall" -f $i.Length, $o.Length)
    } elseif ($i.LastWriteTime -lt $o.LastWriteTime.AddSeconds(-2)) {
        Add-Fail ("Installed DLL older than package ({0} < {1}) - reinstall" -f $i.LastWriteTime, $o.LastWriteTime)
    } else {
        Add-Pass "Installed DLL matches package size/time"
    }
    $outStamp = Get-StampFromFile (Join-Path $PackageDir "BUILD_STAMP.txt")
    $insStamp = Get-StampFromFile (Join-Path $InstalledDir "BUILD_STAMP.txt")
    if ($outStamp -and $insStamp -and ($outStamp -ne $insStamp)) {
        Add-Fail ("Stamp mismatch package='{0}' installed='{1}'" -f $outStamp, $insStamp)
    }
}

Write-Host ""
Write-Host "=== GENESIS Remover health check ===" -ForegroundColor Cyan
Write-Host ("Expected stamp: {0}" -f $ExpectedStamp)
Write-Host ("Repo:           {0}" -f $RepoRoot)
Write-Host ""

Write-Report ("Expected stamp = {0}" -f $ExpectedStamp) "INFO"
Test-PixeraRunning

if (-not $InstalledOnly) {
    Test-Package $PackageDir "out"
    Test-Package $InstalledDir "installed" -MissingIsWarn
} else {
    Test-Package $InstalledDir "installed"
}
Test-PackageVsInstalled
Test-Logs

Write-Host ""
$summary = "RESULT pass={0} warn={1} fail={2}" -f $script:PassCount, $script:WarnCount, $script:FailCount
if ($script:FailCount -gt 0) {
    Write-Host $summary -ForegroundColor Red
    Write-Host "Next: quit PIXERA -> git pull -> BUILD-REMOVER.bat -> INSTALL-PIXERA-REMOVER.bat -> re-run this." -ForegroundColor Yellow
} elseif ($script:WarnCount -gt 0) {
    Write-Host $summary -ForegroundColor Yellow
} else {
    Write-Host $summary -ForegroundColor Green
}

[void]$script:Lines.Add($summary)
$script:Lines | Set-Content -Path $ReportPath -Encoding ASCII
Write-Host ("Report: {0}" -f $ReportPath)
Write-Host ("DLL log:  {0}" -f $DllLog)
Write-Host ("Host log: {0}" -f $HostLog)
Write-Host ""

if ($script:FailCount -gt 0) { exit 1 }
exit 0
