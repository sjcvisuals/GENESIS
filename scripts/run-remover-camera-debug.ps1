# Launch the standalone webcam background-remover (no PIXERA).
# Use this FIRST when Remover looks wrong in PIXERA — it isolates RVM/matte from host I/O.
#
# From the GENESIS repo root (the folder that contains BUILD-AND-RUN.bat):
#   .\scripts\run-remover-camera-debug.ps1
#   .\scripts\run-remover-camera-debug.ps1 -List
#   .\scripts\run-remover-camera-debug.ps1 -Camera 1
#   .\scripts\run-remover-camera-debug.ps1 -Passthrough
#
# Or double-click: RUN-CAMERA-DEBUG.bat
#
# If the right pane shows a live cutout over the checkerboard, matte works outside PIXERA
# and the bug is in ExtGenesisRemover Video In / download / upload.
# If probe says rvm=0, fix ORT + assets/matting first (same files Remover needs).

param(
    [int]$Camera = 0,
    [switch]$List,
    [switch]$Passthrough,
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Exe = Join-Path $RepoRoot "out\RemoverCameraDebug\genesis_remover_camera_debug.exe"

if (-not (Test-Path (Join-Path $RepoRoot "CMakeLists.txt"))) {
    Write-Host ""
    Write-Host "This script must run from the GENESIS repo (folder with BUILD-AND-RUN.bat)." -ForegroundColor Red
    Write-Host "You are resolving repo as: $RepoRoot"
    Write-Host ""
    Write-Host "In Explorer, open your GENESIS folder, then either:"
    Write-Host "  - Double-click RUN-CAMERA-DEBUG.bat"
    Write-Host "  - Or in PowerShell:  cd <path-to-GENESIS>; .\scripts\run-remover-camera-debug.ps1"
    Write-Host ""
    exit 1
}

if ($Build -or -not (Test-Path $Exe)) {
    Write-Host "Building camera debug exe..." -ForegroundColor Cyan
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -CameraDebugOnly
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path $Exe)) {
    Write-Error "Missing $Exe — build failed or sync skipped."
}

$WorkDir = Split-Path -Parent $Exe
$argsList = @()
if ($List) { $argsList += "--list" }
else {
    $argsList += @("--camera", "$Camera")
    if ($Passthrough) { $argsList += "--passthrough" }
}

Write-Host "Launching from $WorkDir" -ForegroundColor Cyan
Write-Host "  exe: $Exe"
Write-Host "  log: $WorkDir\remover-camera-debug.log"
Write-Host "  Left = camera, Right = cutout. Keys: 1/2/3 quality, P passthrough, Esc quit"
Write-Host ""
Write-Host "Interpretation:"
Write-Host "  cutout OK here, broken in PIXERA  -> PIXERA Video In / host texture path"
Write-Host "  rvm=0 in the log                   -> onnxruntime.dll or RVM model missing"
Write-Host "  no cameras / no frames             -> OS camera permission or device busy"
Write-Host ""

Push-Location $WorkDir
try {
    & $Exe @argsList
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
