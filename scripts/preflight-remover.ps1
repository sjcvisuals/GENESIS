#Requires -Version 5.1
# Build + headless-validate ExtGenesisRemover BEFORE PIXERA.
# ASCII-only for Windows PowerShell 5.1.
param(
    [switch]$SkipBuild,
    [switch]$SkipHostSmoke,
    [string]$PixeraRoot = "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73"
)

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$ReportPath = Join-Path $env:TEMP "ExtGenesisRemover.preflight.txt"
$script:Pass = 0
$script:Fail = 0
$script:Warn = 0
$script:Lines = New-Object System.Collections.Generic.List[string]

function Write-Step([string]$Message, [string]$Level = "INFO") {
    $line = "[{0}] {1}" -f $Level, $Message
    [void]$script:Lines.Add($line)
    $color = switch ($Level) {
        "PASS" { "Green" }
        "FAIL" { "Red" }
        "WARN" { "Yellow" }
        default { "Cyan" }
    }
    Write-Host $line -ForegroundColor $color
}
function Add-Pass([string]$m) { $script:Pass++; Write-Step $m "PASS" }
function Add-Fail([string]$m) { $script:Fail++; Write-Step $m "FAIL" }
function Add-Warn([string]$m) { $script:Warn++; Write-Step $m "WARN" }

Write-Host ""
Write-Host "=== GENESIS Remover PREFLIGHT ===" -ForegroundColor Cyan
Write-Host ("Repo: {0}" -f $RepoRoot)
Write-Host ""

Write-Step "--- Static source gates ---"
$matteHost = Join-Path $RepoRoot "extensions\ExtGenesisRemover\matte_host\main.cpp"
$matteIpc = Join-Path $RepoRoot "extensions\ExtGenesisRemover\src\MatteIpc.h"
$rvmSrc = Join-Path $RepoRoot "assets\matting\rvm_mobilenetv3_fp16.onnx"

if (-not (Test-Path -LiteralPath $matteHost)) {
    Add-Fail "matte_host/main.cpp missing"
} else {
    $raw = [System.IO.File]::ReadAllText($matteHost)
    if ($raw -match "<<\s*'\s*[\r\n]+\s*';") {
        Add-Fail "matte_host/main.cpp has broken newline char literal (MSVC C2001/C2137)"
    } elseif ($raw -notmatch "<<\s*'\\n'\s*;") {
        Add-Warn "matte_host log newline pattern not found - verify g_log << full << '\\n';"
    } else {
        Add-Pass "matte_host log newline is a single '\\n' char literal"
    }
    if ($raw -match "--smoke" -and $raw -match "runHostSmoke") {
        Add-Pass "matte_host supports --smoke / runHostSmoke"
    } else {
        Add-Fail "matte_host smoke entry incomplete"
    }
}

$expectedStamp = $null
if (Test-Path -LiteralPath $matteIpc) {
    $ipc = Get-Content -LiteralPath $matteIpc -Raw
    if ($ipc -match 'k\w*Stamp\[\]\s*=\s*"([^"]+)"') {
        $expectedStamp = $Matches[1].Trim()
        Add-Pass ("MatteIpc stamp = {0}" -f $expectedStamp)
    } else {
        Add-Fail "Could not parse kBuildStamp from MatteIpc.h"
    }
} else {
    Add-Fail "MatteIpc.h missing"
}

if (-not (Test-Path -LiteralPath $rvmSrc)) {
    Add-Fail ("RVM model missing: {0}" -f $rvmSrc)
} else {
    $len = (Get-Item -LiteralPath $rvmSrc).Length
    $head = ""
    try { $head = (Get-Content -LiteralPath $rvmSrc -TotalCount 1 -ErrorAction SilentlyContinue) } catch {}
    if ($len -lt 1000000 -or ($head -match 'git-lfs|version https://git-lfs')) {
        Add-Fail ("RVM model is LFS pointer or too small ({0:N0} bytes). Run: git lfs pull" -f $len)
    } else {
        Add-Pass ("RVM model OK ({0:N1} MB)" -f ($len / 1MB))
    }
}

# Unicode punctuation inside string literals breaks MSVC without /utf-8.
$unicodeHits = 0
Get-ChildItem -Path (Join-Path $RepoRoot "extensions\ExtGenesisRemover") -Recurse -Include *.cpp,*.h |
    ForEach-Object {
        $txt = [System.IO.File]::ReadAllText($_.FullName)
        # em-dash / en-dash / arrows / smart quotes as UTF-8 byte sequences
        if ($txt -match '[\u2013\u2014\u2018\u2019\u201C\u201D\u2192]') {
            # Only fail if they appear on non-comment code-ish lines with quotes
            $lineNo = 0
            foreach ($ln in ($txt -split "`n")) {
                $lineNo++
                $trim = $ln.Trim()
                if ($trim.StartsWith("//")) { continue }
                if ($ln -match '"[^"]*[\u2013\u2014\u2018\u2019\u201C\u201D\u2192][^"]*"') {
                    $unicodeHits++
                    if ($unicodeHits -le 8) {
                        Add-Fail ("Unicode in string: {0}:{1}" -f $_.Name, $lineNo)
                    }
                }
            }
        }
    }
if ($unicodeHits -eq 0) {
    Add-Pass "No Unicode punctuation inside ExtGenesisRemover string literals"
} elseif ($unicodeHits -gt 8) {
    Add-Fail ("... and {0} more Unicode string hits" -f ($unicodeHits - 8))
}

if (-not $SkipBuild) {
    Write-Step "--- Build RemoverOnly + matting smoke ---"
    $build = Join-Path $PSScriptRoot "build.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $build -RemoverOnly
    if ($LASTEXITCODE -ne 0) {
        Add-Fail ("build.ps1 -RemoverOnly failed (exit {0}). See build\last-build.log" -f $LASTEXITCODE)
        $script:Lines | Set-Content -Path $ReportPath -Encoding ASCII
        Write-Host ("Report: {0}" -f $ReportPath)
        exit 1
    }
    Add-Pass "RemoverOnly build succeeded"
} else {
    Add-Warn "SkipBuild set - using existing out/"
}

$pkg = Join-Path $RepoRoot "out\ExtGenesisRemover"
$smokeCandidates = @(
    (Join-Path $RepoRoot "out\ExtGenesisRemover\genesis_matting_smoke.exe"),
    (Join-Path $RepoRoot "out\genesis_matting_smoke.exe"),
    (Join-Path $RepoRoot "build\Release\genesis_matting_smoke.exe"),
    (Join-Path $RepoRoot "build\RelWithDebInfo\genesis_matting_smoke.exe"),
    (Join-Path $RepoRoot "build\rxExt\ExtGenesisRemover\Release\genesis_matting_smoke.exe")
)
$smokeExe = $smokeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$hostExe = Join-Path $pkg "GenesisRemoverMatteHost.exe"
$dll = Join-Path $pkg "ExtGenesisRemover.dll"

if (Test-Path -LiteralPath $dll) { Add-Pass ("Package DLL present ({0})" -f $dll) } else { Add-Fail "Package DLL missing (out\ExtGenesisRemover)" }
if (Test-Path -LiteralPath $hostExe) { Add-Pass ("Package host present ({0})" -f $hostExe) } else { Add-Fail "Package host missing" }

Write-Step "--- Headless matting smoke (--require-rvm) ---"
if (-not $smokeExe) {
    Add-Fail "genesis_matting_smoke.exe not found after build"
} else {
    Write-Step ("Running {0} --require-rvm" -f $smokeExe)
    Push-Location (Split-Path -Parent $smokeExe)
    try {
        $p = Start-Process -FilePath $smokeExe -ArgumentList @("--require-rvm") -Wait -PassThru -NoNewWindow
        if ($p.ExitCode -eq 0) { Add-Pass "matting smoke PASS (RVM required)" }
        else { Add-Fail ("matting smoke FAILED exit={0}" -f $p.ExitCode) }
    } finally { Pop-Location }
}

if (-not $SkipHostSmoke) {
    Write-Step "--- MatteHost --smoke ---"
    if (-not (Test-Path -LiteralPath $hostExe)) {
        Add-Fail "Cannot run host smoke - exe missing"
    } else {
        Write-Step ("Running {0} --smoke" -f $hostExe)
        $p = Start-Process -FilePath $hostExe -ArgumentList @("--smoke") -Wait -PassThru -NoNewWindow
        if ($p.ExitCode -eq 0) { Add-Pass "MatteHost --smoke PASS" }
        else { Add-Fail ("MatteHost --smoke FAILED exit={0}. See %TEMP%\GenesisRemoverMatteHost.log" -f $p.ExitCode) }
    }
} else {
    Add-Warn "SkipHostSmoke set"
}

Write-Step "--- Package health-check ---"
$health = Join-Path $PSScriptRoot "health-check-remover.ps1"
if (-not (Test-Path -LiteralPath $health)) {
    Add-Fail "scripts/health-check-remover.ps1 missing"
} else {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $health -PackageDir $pkg -PixeraRoot $PixeraRoot
    if ($LASTEXITCODE -ne 0) { Add-Fail "health-check-remover.ps1 reported failures" }
    else { Add-Pass "health-check-remover.ps1 pass/warn only" }
}

$summary = "PREFLIGHT RESULT pass={0} warn={1} fail={2} stamp={3}" -f $script:Pass, $script:Warn, $script:Fail, $expectedStamp
[void]$script:Lines.Add($summary)
$script:Lines | Set-Content -Path $ReportPath -Encoding ASCII
Write-Host ""
if ($script:Fail -gt 0) {
    Write-Host $summary -ForegroundColor Red
    Write-Host ("Report: {0}" -f $ReportPath)
    Write-Host "Fix FAIL lines before INSTALL-PIXERA-REMOVER.bat / PIXERA."
    exit 1
}
Write-Host $summary -ForegroundColor Green
Write-Host ("Report: {0}" -f $ReportPath)
Write-Host "Safe next step: INSTALL-PIXERA-REMOVER.bat (quit PIXERA first)."
exit 0
