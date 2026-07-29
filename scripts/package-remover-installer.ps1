# Build ExtGenesisRemover and produce free download artifacts:
#   GenesisRemoverSetup-<ver>.exe  (Inno Setup wizard)
#   GenesisRemover-<ver>.zip       (portable — browse DLL from PIXERA)
param(
    [switch]$BuildOnly,
    [switch]$SkipBuild,
    [switch]$ZipOnly,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
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

$version = "0.1.0"

Push-Location $RepoRoot
try {
    if (-not $SkipBuild) {
        Write-Host "Configuring Genesis Remover-only ($Configuration)..."
        & $CMake -B $BuildDir -DCMAKE_BUILD_TYPE=$Configuration -DGENESIS_REMOVER_ONLY=ON
        Write-Host "Building ExtGenesisRemover..."
        & $CMake --build $BuildDir --config $Configuration --target ExtGenesisRemover
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed with exit code $LASTEXITCODE"
        }
    }

    & (Join-Path $PSScriptRoot "stage-remover-installer.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Configuration $Configuration

    $packageDir = Join-Path $BuildDir "package"
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

    # Portable ZIP — shareable free download without Inno Setup
    $zipPath = Join-Path $packageDir "GenesisRemover-$version.zip"
    $zipSource = Join-Path $BuildDir "deploy\GenesisRemover"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $zipSource "*") -DestinationPath $zipPath -Force
    $zipMb = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
    Write-Host "Portable ZIP ready: $zipPath ($zipMb MB)"

    if ($BuildOnly -or $ZipOnly) {
        Write-Host "Staging/ZIP complete (skipping Inno Setup)."
        return
    }

    $IsccCandidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $Iscc = $IsccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Iscc) {
        Write-Warning @"
Inno Setup 6 not found (https://jrsoftware.org/isinfo.php).
Portable ZIP is still ready for free distribution:
  $zipPath
To build the wizard installer later:
  .\scripts\package-remover-installer.ps1 -SkipBuild
"@
        return
    }

    $stagingDir = Join-Path $BuildDir "package\remover-staging"
    Write-Host "Compiling Genesis Remover installer with Inno Setup (v$version)..."
    & $Iscc (Join-Path $RepoRoot "installer\GenesisRemover.iss") "/DStagingDir=$stagingDir" "/DMyAppVersion=$version"
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
    }

    $installer = Join-Path $packageDir "GenesisRemoverSetup-$version.exe"
    if (Test-Path $installer) {
        $sizeMb = [math]::Round((Get-Item $installer).Length / 1MB, 2)
        Write-Host ""
        Write-Host "Free download artifacts:"
        Write-Host "  Installer: $installer ($sizeMb MB)"
        Write-Host "  Portable:  $zipPath ($zipMb MB)"
        Write-Host ""
        Write-Host "Publish either file as a GitHub Release asset for a public download link."
    } else {
        throw "Installer output not found at $installer"
    }
}
finally {
    Pop-Location
}
