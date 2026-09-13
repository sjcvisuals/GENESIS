# Build + launch ExtGenesis inside the RenderBridge RX harness (optional).

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot "build.ps1") -PixeraOnly
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$OutDllDir = Join-Path $RepoRoot "out\ExtGenesis"
$BuildDll = Join-Path $OutDllDir "ExtGenesis.dll"
if (-not (Test-Path $BuildDll)) {
    throw "Missing $BuildDll - run BUILD-AND-RUN.bat first."
}

$RxRoot = Join-Path $RepoRoot "third_party\render-bridge-sdk\RenderBridge\RX"
$RxExe = Join-Path $RxRoot "RX.exe"
$DeployDir = Join-Path $RxRoot "modules\ExtGenesis"
$Script = Join-Path $RepoRoot "extensions\ExtGenesis\run.lua"

if (-not (Test-Path $RxExe)) {
    throw "RX.exe not found at $RxExe"
}

New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null
Copy-Item -Path (Join-Path $OutDllDir "*") -Destination $DeployDir -Recurse -Force
Copy-Item -Path $BuildDll -Destination (Join-Path $RxRoot "modules\ExtGenesis.dll") -Force

Write-Host "Launching RX harness with ExtGenesis..."
Start-Process -FilePath $RxExe -WorkingDirectory $RxRoot -ArgumentList "-script=$Script"
