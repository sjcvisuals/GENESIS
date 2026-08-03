# Copy the WIP Remover package into PIXERA's data\rxExt folder (same place ExtGenesis lives).
#
# Example:
#   .\scripts\install-pixera-remover.ps1
#   .\scripts\install-pixera-remover.ps1 -PixeraRoot "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73"

param(
    [string]$PixeraRoot = "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73",
    [string]$SourceDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) {
    $SourceDir = Join-Path $RepoRoot "out\ExtGenesisRemover"
}
$SourceDir = (Resolve-Path $SourceDir).Path

$dest = Join-Path $PixeraRoot "data\rxExt\ExtGenesisRemover"
if (-not (Test-Path (Join-Path $SourceDir "ExtGenesisRemover.dll"))) {
    throw "Missing ExtGenesisRemover.dll in $SourceDir - run BUILD-AND-RUN.bat first"
}
if (-not (Test-Path (Join-Path $PixeraRoot "data\rxExt"))) {
    throw "PIXERA rxExt folder not found: $(Join-Path $PixeraRoot 'data\rxExt')"
}

Write-Host "Source: $SourceDir"
Write-Host "Dest:   $dest"

if (Test-Path $dest) {
    Remove-Item -Path $dest -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $dest -Recurse -Force

Get-ChildItem -Path $dest -Recurse -Include *.dll,*.exe -ErrorAction SilentlyContinue |
    ForEach-Object {
        try { Unblock-File -Path $_.FullName -ErrorAction SilentlyContinue } catch { }
    }

# Clear old log so the next PIXERA run only shows this install's stamp.
$oldLog = Join-Path $env:TEMP "ExtGenesisRemover.log"
if (Test-Path $oldLog) {
    Remove-Item $oldLog -Force -ErrorAction SilentlyContinue
    Write-Host "Cleared old log: $oldLog"
}

Write-Host ""
Write-Host "Installed. In PIXERA:" -ForegroundColor Green
Write-Host "  1. Fully quit PIXERA"
Write-Host "  2. Delete the old GENESIS Remover resource from the project (if present)"
Write-Host "  3. Re-import / browse to:"
Write-Host "       $(Join-Path $dest 'ExtGenesisRemover.dll')"
Write-Host "  4. Do NOT use the ExtGenesis folder for this DLL"
Write-Host ""
$dll = Get-Item (Join-Path $dest "ExtGenesisRemover.dll")
Write-Host ("DLL LastWriteTime: {0}" -f $dll.LastWriteTime)
Write-Host ("DLL Size:          {0:N0} bytes" -f $dll.Length)
if (Test-Path (Join-Path $dest "BUILD_STAMP.txt")) {
    Write-Host "BUILD_STAMP.txt:"
    Get-Content (Join-Path $dest "BUILD_STAMP.txt") | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""
Write-Host "Color meanings while testing:"
Write-Host "  CYAN    = DLL rendering, waiting for Video In download"
Write-Host "  MAGENTA = Video In linked but pixels are empty"
Write-Host "  Webcam  = passthrough working; cutout follows after RVM warms up"
Write-Host ""
Write-Host "Stamp must be: remover-timeline-v8c-link-alloc-20260803"
Write-Host "Dump log with: .\scripts\dump-remover-log.ps1"
Write-Host ("Check DLL: .\scripts\check-pixera-dll.ps1 -DllPath `"{0}`"" -f (Join-Path $dest "ExtGenesisRemover.dll"))
