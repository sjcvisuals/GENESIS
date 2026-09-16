# Pack out\ExtGenesisRemover into a portable zip with INSTALL.bat path picker.
#
# Usage (after BUILD-REMOVER.bat):
#   .\PACK-REMOVER-PORTABLE.bat
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pack-remover-portable.ps1

param(
    [string]$SourceDir = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $SourceDir) {
    $SourceDir = Join-Path $RepoRoot "out\ExtGenesisRemover"
}
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "out"
}

$dll = Join-Path $SourceDir "ExtGenesisRemover.dll"
$hostExe = Join-Path $SourceDir "GenesisRemoverMatteHost.exe"
$ort = Join-Path $SourceDir "onnxruntime.dll"
$rvm = Join-Path $SourceDir "assets\matting\rvm_mobilenetv3_fp16.onnx"
$template = Join-Path $RepoRoot "deploy\portable-remover"

foreach ($p in @($dll, $hostExe, $ort, $rvm)) {
    if (-not (Test-Path -LiteralPath $p)) {
        throw "Missing $p - run BUILD-REMOVER.bat first"
    }
}
if (-not (Test-Path -LiteralPath $template)) {
    throw "Missing portable template: $template"
}

$rvmLen = (Get-Item -LiteralPath $rvm).Length
if ($rvmLen -lt 1000000) {
    throw ("RVM model too small ({0:N0} bytes) - run git lfs pull then rebuild" -f $rvmLen)
}

$stamp = "remover-demo-unknown"
$stampCpp = Join-Path $RepoRoot "extensions\ExtGenesisRemover\src\BuildStamp.cpp"
if (Test-Path -LiteralPath $stampCpp) {
    $t = Get-Content -LiteralPath $stampCpp -Raw -ErrorAction SilentlyContinue
    if ($t -match 'kBuildStamp\[\]\s*=\s*"([^"]+)"') {
        $stamp = $Matches[1].Trim()
    }
}
$binBytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $dll))
$binText = [System.Text.Encoding]::ASCII.GetString($binBytes)
$binStamps = @([regex]::Matches($binText, 'remover-demo-v[0-9][a-z0-9\-]+') |
    ForEach-Object { $_.Value } | Sort-Object -Unique)
if ($binStamps -notcontains $stamp) {
    throw ("DLL binary stamp [{0}] != source stamp {1}. Rebuild before packing." -f `
        ($binStamps -join ', '), $stamp)
}

$safeStamp = ($stamp -replace '[^\w\-]+', '-')
$stageName = "GenesisRemover-Portable-$safeStamp"
$stage = Join-Path $OutDir $stageName
$zipPath = Join-Path $OutDir ($stageName + ".zip")

Write-Host "=== Pack GENESIS Remover portable ===" -ForegroundColor Cyan
Write-Host ("Source:  {0}" -f $SourceDir)
Write-Host ("Stamp:   {0}" -f $stamp)
Write-Host ("Stage:   {0}" -f $stage)
Write-Host ("Zip:     {0}" -f $zipPath)

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "package") | Out-Null

Copy-Item -LiteralPath (Join-Path $template "INSTALL.bat") -Destination (Join-Path $stage "INSTALL.bat") -Force
Copy-Item -LiteralPath (Join-Path $template "install.ps1") -Destination (Join-Path $stage "install.ps1") -Force
Copy-Item -LiteralPath (Join-Path $template "README.txt") -Destination (Join-Path $stage "README.txt") -Force

# Payload
& robocopy $SourceDir (Join-Path $stage "package") /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "robocopy package payload failed (exit $LASTEXITCODE)"
}

$stampNote = @"
pack_stamp=$stamp
pack_time=$(Get-Date -Format o)
source=$SourceDir
dll_bytes=$((Get-Item -LiteralPath $dll).Length)
host_bytes=$((Get-Item -LiteralPath $hostExe).Length)
"@
Set-Content -Path (Join-Path $stage "PACKAGE_INFO.txt") -Value $stampNote -Encoding ASCII
Set-Content -Path (Join-Path $stage "package\PACKAGED_FROM.txt") -Value $stampNote -Encoding ASCII

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPath -Force

$zipItem = Get-Item -LiteralPath $zipPath
Write-Host ""
Write-Host "Portable package ready." -ForegroundColor Green
Write-Host ("  Folder: {0}" -f $stage)
Write-Host ("  Zip:    {0} ({1:N1} MB)" -f $zipPath, ($zipItem.Length / 1MB))
Write-Host "Copy the zip (or folder) to the other machine, unzip, Run INSTALL.bat as admin."
Write-Host "Installer will ask for that machine's PIXERA root (data\rxExt parent)."
