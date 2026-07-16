# Build binaries and produce GenesisSetup.exe (Inno Setup).
param(
    [switch]$BuildOnly,
    [switch]$SkipBuild,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build"
$CMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $CMake)) {
    $CMake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path $CMake)) {
    $found = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "**\cmake.exe" 2>$null | Select-Object -First 1
    if ($found) { $CMake = $found }
}
if (-not (Test-Path $CMake)) {
    Write-Error "CMake not found. Install Visual Studio 2022 with the C++ workload."
}

$VulkanRoot = Join-Path $env:LOCALAPPDATA "VulkanSDK\1.4.350.0"
if (Test-Path $VulkanRoot) {
    $env:VULKAN_SDK = $VulkanRoot
}

$version = "0.1.1"
$packageTargets = @(
    "genesis_gltf_viewer",
    "genesis_player",
    "genesis_multiview_player",
    "genesis_topology_validate",
    "genesis_determinism_harness",
    "ExtGenesis"
)

Push-Location $RepoRoot
try {
    if (-not $SkipBuild) {
        Write-Host "Configuring GENESIS ($Configuration)..."
        & $CMake -B $BuildDir -DCMAKE_BUILD_TYPE=$Configuration
        Write-Host "Building installer targets: $($packageTargets -join ', ')..."
        & $CMake --build $BuildDir --config $Configuration --target @packageTargets
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed with exit code $LASTEXITCODE"
        }
    }

    & (Join-Path $PSScriptRoot "stage-installer.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Configuration $Configuration

    if ($BuildOnly) {
        Write-Host "Staging complete (BuildOnly - skipping Inno Setup)."
        return
    }

    $IsccCandidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $Iscc = $IsccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Iscc) {
        Write-Error @"
Inno Setup 6 not found. Install from https://jrsoftware.org/isinfo.php
Then re-run: .\scripts\package-installer.ps1
Staged files are ready at: $BuildDir\package\staging
"@
    }

    $stagingDir = Join-Path $BuildDir "package\staging"
    Write-Host "Compiling installer with Inno Setup (v$version)..."
    & $Iscc (Join-Path $RepoRoot "installer\Genesis.iss") "/DStagingDir=$stagingDir" "/DMyAppVersion=$version"
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
    }

    $installer = Join-Path $BuildDir "package\GenesisSetup-$version.exe"
    if (Test-Path $installer) {
        $sizeMb = [math]::Round((Get-Item $installer).Length / 1MB, 2)
        Write-Host ""
        Write-Host "Installer ready: $installer ($sizeMb MB)"
    } else {
        throw "Installer output not found at $installer"
    }
}
finally {
    Pop-Location
}
