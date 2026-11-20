# Build binaries and produce GenesisSetup-*.exe (Inno Setup wizard).
# Optional secondary artifact: portable zip for advanced users.
param(
    [switch]$BuildOnly,
    [switch]$SkipBuild,
    [switch]$SkipPortableZip,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}

function Get-GenesisVersion {
    param([string]$Root)
    $cmake = Join-Path $Root "CMakeLists.txt"
    $match = Select-String -Path $cmake -Pattern 'project\s*\(\s*GENESIS\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
        Select-Object -First 1
    if (-not $match) {
        throw "Could not read GENESIS VERSION from CMakeLists.txt"
    }
    return $match.Matches[0].Groups[1].Value
}

if (-not $Version) {
    $Version = Get-GenesisVersion -Root $RepoRoot
}

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
        Write-Host "Configuring GENESIS ($Configuration, v$Version)..."
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
    $packageDir = Join-Path $BuildDir "package"
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

    Write-Host "Compiling installer with Inno Setup (v$Version)..."
    & $Iscc (Join-Path $RepoRoot "installer\Genesis.iss") "/DStagingDir=$stagingDir" "/DMyAppVersion=$Version"
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
    }

    $installer = Join-Path $packageDir "GenesisSetup-$Version.exe"
    if (-not (Test-Path $installer)) {
        throw "Installer output not found at $installer"
    }
    $sizeMb = [math]::Round((Get-Item $installer).Length / 1MB, 2)

    $portableZip = $null
    if (-not $SkipPortableZip) {
        $portableZip = Join-Path $packageDir "GENESIS-$Version-windows-x64-portable.zip"
        if (Test-Path $portableZip) {
            Remove-Item -Path $portableZip -Force
        }
        Write-Host "Creating portable zip (secondary / advanced)..."
        Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $portableZip -CompressionLevel Optimal
    }

    Write-Host ""
    Write-Host "========================================"
    Write-Host "  Primary (ship this):  $installer ($sizeMb MB)"
    if ($portableZip -and (Test-Path $portableZip)) {
        $zipMb = [math]::Round((Get-Item $portableZip).Length / 1MB, 2)
        Write-Host "  Portable (optional):  $portableZip ($zipMb MB)"
    }
    Write-Host "========================================"
    Write-Host "Attach GenesisSetup-$Version.exe to the GitHub Release."
    Write-Host "End-user steps: docs\install-windows.md"
}
finally {
    Pop-Location
}
