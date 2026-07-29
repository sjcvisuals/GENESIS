# GENESIS one-shot Windows build helper
#
# Usage (from the repo root, in PowerShell):
#   .\scripts\build.ps1              # editor + Pixera DLL
#   .\scripts\build.ps1 -Run         # build then launch the editor
#   .\scripts\build.ps1 -Config Release
#   .\scripts\build.ps1 -PixeraOnly  # ExtGenesis.dll only
#   .\scripts\build.ps1 -EditorOnly  # editor only
#
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Run,
    [switch]$EditorOnly,
    [switch]$PixeraOnly
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build"

function Find-CMake {
    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "${env:ProgramFiles}\CMake\bin\cmake.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "**/cmake.exe" 2>$null | Select-Object -First 1
        if ($found -and (Test-Path $found)) { return $found }
    }
    $inPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }
    return $null
}

$CMake = Find-CMake
if (-not $CMake) {
    Write-Host ""
    Write-Host "CMake / Visual Studio C++ tools not found." -ForegroundColor Red
    Write-Host "Install Visual Studio 2022 with the 'Desktop development with C++' workload,"
    Write-Host "then re-run this script."
    Write-Host ""
    Write-Host "Download: https://visualstudio.microsoft.com/downloads/"
    exit 1
}

# Prefer a local Vulkan SDK (glslc). Optional bootstrap from LunarG installer.
$VulkanRoot = Join-Path $env:LOCALAPPDATA "VulkanSDK\1.4.350.0"
$Glslc = Join-Path $VulkanRoot "Bin\glslc.exe"
if (-not (Test-Path $Glslc)) {
    Write-Host "glslc not found under $VulkanRoot - attempting to extract from Vulkan SDK installer..."
    $Installer = Join-Path $env:TEMP "vulkansdk-windows-X64-1.4.350.0.exe"
    if (-not (Test-Path $Installer)) {
        Write-Host "Downloading Vulkan SDK installer (one-time)..."
        curl.exe -L -o $Installer "https://sdk.lunarg.com/sdk/download/1.4.350.0/windows/vulkansdk-windows-X64-1.4.350.0.exe"
    }
    $SevenZip = "${env:ProgramFiles}\7-Zip\7z.exe"
    if (-not (Test-Path $SevenZip)) {
        Write-Host "Installing 7-Zip (needed to extract glslc)..."
        winget install 7zip.7zip --accept-source-agreements --accept-package-agreements --disable-interactivity
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $VulkanRoot "Bin") | Out-Null
    & $SevenZip x $Installer "Bin\glslc.exe" "-o$VulkanRoot" -y | Out-Null
}
if (Test-Path $Glslc) {
    $env:VULKAN_SDK = $VulkanRoot
    Write-Host "VULKAN_SDK=$env:VULKAN_SDK"
} else {
    Write-Host "Warning: glslc still missing. If configure fails, install the Vulkan SDK from https://vulkan.lunarg.com/" -ForegroundColor Yellow
}

$targets = @()
if ($PixeraOnly) {
    $targets = @("ExtGenesis")
} elseif ($EditorOnly) {
    $targets = @("genesis_gltf_viewer")
} else {
    $targets = @("genesis_gltf_viewer", "ExtGenesis")
}

Write-Host ""
Write-Host "=== GENESIS build ($Config) ===" -ForegroundColor Cyan
Write-Host "Repo:    $RepoRoot"
Write-Host "CMake:   $CMake"
Write-Host "Targets: $($targets -join ', ')"
Write-Host ""

Push-Location $RepoRoot
try {
    & $CMake -B $BuildDir -DCMAKE_BUILD_TYPE=$Config
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    & $CMake --build $BuildDir --config $Config --target @targets
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

function Resolve-BuiltExe([string]$Name) {
    $candidates = @(
        (Join-Path $BuildDir "$Config\$Name"),
        (Join-Path $BuildDir $Name),
        (Join-Path $BuildDir "RelWithDebInfo\$Name"),
        (Join-Path $BuildDir "MinSizeRel\$Name")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Resolve-BuiltDll([string]$Relative) {
    $candidates = @(
        (Join-Path $BuildDir $Relative),
        (Join-Path $BuildDir "$Config\$Relative"),
        (Join-Path $BuildDir "rxExt\ExtGenesis\$Config\ExtGenesis.dll"),
        (Join-Path $BuildDir "rxExt\ExtGenesis\ExtGenesis.dll"),
        (Join-Path $BuildDir "deploy\GenesisPixera\ExtGenesis.dll")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

$Editor = Resolve-BuiltExe "genesis_gltf_viewer.exe"
$Dll = Resolve-BuiltDll "rxExt\ExtGenesis\$Config\ExtGenesis.dll"
if (-not $Dll) { $Dll = Resolve-BuiltDll "deploy\GenesisPixera\ExtGenesis.dll" }

Write-Host ""
Write-Host "=== Build finished ===" -ForegroundColor Green
if ($Editor) {
    Write-Host "Editor exe:"
    Write-Host "  $Editor"
    Write-Host "Run (keep working directory = folder of the exe so assets resolve):"
    Write-Host ('  "{0}"' -f $Editor)
    Write-Host "Then: File -> Examples"
} else {
    Write-Host "Editor exe not found - search with:"
    Write-Host "  Get-ChildItem -Recurse -Filter genesis_gltf_viewer.exe $BuildDir"
}
if ($Dll) {
    Write-Host ""
    Write-Host "Pixera DLL (browse to this in PIXERA generic extension):"
    Write-Host "  $Dll"
    Write-Host "Keep the sibling assets\ / scenes\ folders next to that DLL."
} elseif (-not $EditorOnly) {
    Write-Host ""
    Write-Host "ExtGenesis.dll not found (Windows-only target). If you expected it, open the build log above."
}
Write-Host ""

if ($Run) {
    if (-not $Editor) { Write-Error "Cannot -Run: editor exe missing." }
    $EditorDir = Split-Path -Parent $Editor
    Write-Host "Launching editor from $EditorDir ..."
    Start-Process -FilePath $Editor -WorkingDirectory $EditorDir
}
