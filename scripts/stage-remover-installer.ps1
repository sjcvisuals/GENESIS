# Stage ExtGenesisRemover payload for the free Windows installer + portable ZIP.
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "build"),
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

function Resolve-BuildPath {
    param([string]$Relative)
    $paths = @(
        (Join-Path $BuildDir $Relative),
        (Join-Path $BuildDir "$Configuration\$Relative")
    )
    foreach ($path in $paths) {
        if (Test-Path $path) { return (Resolve-Path $path).Path }
    }
    return $null
}

$ExtDll = Resolve-BuildPath "rxExt\ExtGenesisRemover\$Configuration\ExtGenesisRemover.dll"
if (-not $ExtDll) {
    $ExtDll = Resolve-BuildPath "rxExt\ExtGenesisRemover\ExtGenesisRemover.dll"
}
if (-not $ExtDll) {
    $ExtDll = Resolve-BuildPath "deploy\GenesisRemover\ExtGenesisRemover.dll"
}
if (-not $ExtDll) {
    throw "Missing ExtGenesisRemover.dll ($Configuration). Build with: .\scripts\build.ps1 -RemoverOnly -Config $Configuration"
}

$DeployPkg = Resolve-BuildPath "deploy\GenesisRemover"
$MattingAssets = Join-Path $RepoRoot "assets\matting"
if (-not (Test-Path (Join-Path $MattingAssets "rvm_mobilenetv3_fp16.onnx"))) {
    Write-Host "RVM model missing — running fetch-rvm-model.sh via bash if available..."
    $fetch = Join-Path $RepoRoot "scripts\fetch-rvm-model.sh"
    if (Test-Path $fetch) {
        & bash $fetch
    }
}
if (-not (Test-Path (Join-Path $MattingAssets "rvm_mobilenetv3_fp16.onnx"))) {
    throw "Missing assets/matting/rvm_mobilenetv3_fp16.onnx — run scripts/fetch-rvm-model.sh"
}

$StageRoot = Join-Path $BuildDir "package\remover-staging"
if (Test-Path $StageRoot) {
    Remove-Item -Path $StageRoot -Recurse -Force
}

$extStage = Join-Path $StageRoot "ExtGenesisRemover"
New-Item -ItemType Directory -Force -Path (Join-Path $extStage "assets\matting") | Out-Null

Copy-Item -Path $ExtDll -Destination (Join-Path $extStage "ExtGenesisRemover.dll") -Force
Copy-Item -Path (Join-Path $RepoRoot "deploy\GenesisRemover.manifest.json") -Destination $extStage -Force
Copy-Item -Path (Join-Path $RepoRoot "deploy\README-remover.txt") -Destination (Join-Path $extStage "README.txt") -Force
Copy-Item -Path (Join-Path $RepoRoot "deploy\HOW-TO-PIXERA-REMOVER.txt") -Destination (Join-Path $extStage "HOW-TO-PIXERA.txt") -Force
Copy-Item -Path (Join-Path $MattingAssets "*") -Destination (Join-Path $extStage "assets\matting") -Force -Recurse

# Prefer ORT already staged by CMake post-build next to the deploy package.
$OrtCandidates = @(
    (Join-Path $DeployPkg "onnxruntime.dll"),
    (Join-Path (Split-Path -Parent $ExtDll) "onnxruntime.dll")
)
$ortCopied = $false
foreach ($cand in $OrtCandidates) {
    if ($cand -and (Test-Path $cand)) {
        Copy-Item -Path $cand -Destination (Join-Path $extStage "onnxruntime.dll") -Force
        $ortCopied = $true
        break
    }
}
if (-not $ortCopied) {
    Write-Warning "onnxruntime.dll not found beside the Remover package. RVM may fall back to chroma key unless you copy it manually."
}

# VC++ redistributable
$redistDir = Join-Path $StageRoot "redist"
New-Item -ItemType Directory -Force -Path $redistDir | Out-Null
$redistExe = Join-Path $redistDir "vc_redist.x64.exe"
$bundled = Join-Path $RepoRoot "installer\redist\vc_redist.x64.exe"
if (Test-Path $bundled) {
    Copy-Item -Path $bundled -Destination $redistExe -Force
} else {
    Write-Host "Downloading VC++ 2015-2022 redistributable..."
    $url = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    Invoke-WebRequest -Uri $url -OutFile $redistExe -UseBasicParsing
    New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot "installer\redist") | Out-Null
    Copy-Item -Path $redistExe -Destination $bundled -Force
}

# Also refresh the ready-to-copy deploy folder used for portable ZIP.
$zipSource = Join-Path $BuildDir "deploy\GenesisRemover"
if (Test-Path $zipSource) {
    Remove-Item -Path $zipSource -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $zipSource | Out-Null
Copy-Item -Path (Join-Path $extStage "*") -Destination $zipSource -Recurse -Force

Write-Host "Staged Genesis Remover installer payload: $StageRoot"
Write-Host "  Extension: $extStage"
Write-Host "  Portable:  $zipSource"
Write-Host "  Config:    $Configuration"
