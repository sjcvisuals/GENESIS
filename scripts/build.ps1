# GENESIS build helper (Windows)
# Installs Vulkan runtime if missing, configures, and builds.

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$CMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path $CMake)) {
    Write-Error "CMake not found. Install Visual Studio 2022 with the C++ workload."
}

# Vulkan SDK root — glslc extracted from installer if full SDK is unavailable
$VulkanRoot = Join-Path $env:LOCALAPPDATA "VulkanSDK\1.4.350.0"
$Glslc = Join-Path $VulkanRoot "Bin\glslc.exe"

if (-not (Test-Path $Glslc)) {
    Write-Host "glslc not found. Extracting from Vulkan SDK installer..."

    $Installer = Join-Path $env:TEMP "vulkansdk-windows-X64-1.4.350.0.exe"
    if (-not (Test-Path $Installer)) {
        Write-Host "Downloading Vulkan SDK installer..."
        curl.exe -L -o $Installer "https://sdk.lunarg.com/sdk/download/1.4.350.0/windows/vulkansdk-windows-X64-1.4.350.0.exe"
    }

    $SevenZip = "C:\Program Files\7-Zip\7z.exe"
    if (-not (Test-Path $SevenZip)) {
        winget install 7zip.7zip --accept-source-agreements --accept-package-agreements --disable-interactivity
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $VulkanRoot "Bin") | Out-Null
    & $SevenZip x $Installer "Bin\glslc.exe" "-o$VulkanRoot" -y | Out-Null
}

if (-not (Test-Path $Glslc)) {
    Write-Error "Failed to provision glslc. Install the Vulkan SDK manually."
}

# Vulkan runtime (driver loader)
$VulkanRt = Get-ItemProperty "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers\VulkanRT" -ErrorAction SilentlyContinue
if (-not $VulkanRt) {
    Write-Host "Installing Vulkan runtime..."
    winget install KhronosGroup.VulkanRT --accept-source-agreements --accept-package-agreements --disable-interactivity
}

$env:VULKAN_SDK = $VulkanRoot
Write-Host "VULKAN_SDK=$env:VULKAN_SDK"

Push-Location $RepoRoot
try {
    & $CMake -B build -DCMAKE_BUILD_TYPE=Debug
    & $CMake --build build --config Debug
    Write-Host ""
    Write-Host "Build complete. Run:"
    Write-Host "  .\build\Debug\genesis_triangle.exe"
}
finally {
    Pop-Location
}
