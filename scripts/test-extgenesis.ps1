# Deploy and launch ExtGenesis with the PIXERA RenderBridge SDK test harness (RX.exe).
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent
$CMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$RxDir = Join-Path $RepoRoot "third_party\render-bridge-sdk\RenderBridge\RX"
$Script = Join-Path $RepoRoot "extensions\ExtGenesis\run.lua"
$BuildDll = Join-Path $RepoRoot "build\rxExt\ExtGenesis\Debug\ExtGenesis.dll"
$ModulesDir = Join-Path $RxDir "modules"
$DeployDir = Join-Path $ModulesDir "ExtGenesis"

if (-not (Test-Path $CMake)) {
    Write-Error "CMake not found. Install Visual Studio 2022 with the C++ workload."
}
if (-not (Test-Path (Join-Path $RxDir "RX.exe"))) {
    Write-Error "RX.exe not found at $RxDir"
}

Write-Host "Building ExtGenesis..."
& $CMake --build (Join-Path $RepoRoot "build") --config Debug --target ExtGenesis
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path $BuildDll)) {
    Write-Error "Build output not found: $BuildDll"
}

Write-Host "Deploying to RX modules..."
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null
Copy-Item $BuildDll (Join-Path $DeployDir "ExtGenesis.dll") -Force
Copy-Item $BuildDll (Join-Path $ModulesDir "ExtGenesis.dll") -Force
$AssetsSrc = Join-Path $RepoRoot "build\rxExt\ExtGenesis\Debug\assets"
if (Test-Path $AssetsSrc) {
    Copy-Item $AssetsSrc (Join-Path $DeployDir "assets") -Recurse -Force
}

Write-Host ""
Write-Host "Launching RX test harness..."
Write-Host "  RX:     $RxDir\RX.exe"
Write-Host "  Script: $Script"
Write-Host ""
Write-Host "Expected: a topmost 'Display' window showing the GENESIS demo scene."
Write-Host "Close the window (or press Escape) to exit."
Write-Host ""

Push-Location $RxDir
try {
    & "$RxDir\RX.exe" -script=$Script
    Write-Host "RX exited with code $LASTEXITCODE"
} finally {
    Pop-Location
}
