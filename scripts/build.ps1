# GENESIS local WIP build (Windows)
#
# Day-to-day: double-click BUILD-AND-RUN.bat in the repo root.
# That builds the editor (Debug) + Pixera DLLs (Release), syncs into out\,
# and launches the editor.
#
# PowerShell:
#   .\scripts\build.ps1          # build only
#   .\scripts\build.ps1 -Run     # build + launch editor
#
# Ignore the build\ tree for testing - use out\ only.
# Pixera DLLs are always Release: Debug CRTs often fail to LoadLibrary in PIXERA.

param(
    [switch]$Run,
    [switch]$EditorOnly,
    [switch]$PixeraOnly,
    [switch]$RemoverOnly
)

$ErrorActionPreference = "Stop"

# Editor stays Debug for WIP. Pixera host apps need Release CRT DLLs.
$EditorConfig = "Debug"
$PixeraConfig = "Release"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build"
$OutDir = Join-Path $RepoRoot "out"

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

function Invoke-LoggedNative {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Command,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [switch]$Append
    )
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = & $Command 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.ToString() }
            else { "$_" }
        }
        if ($Append) {
            $lines | Tee-Object -FilePath $LogPath -Append | Out-Host
        } else {
            $lines | Tee-Object -FilePath $LogPath | Out-Host
        }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }
}

function Resolve-BuiltExe([string]$Name, [string]$Config) {
    foreach ($c in @(
        (Join-Path $BuildDir "$Config\$Name"),
        (Join-Path $BuildDir $Name)
    )) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Resolve-BuiltDll([string]$Name, [string]$Config) {
    $candidates = @(
        (Join-Path $BuildDir "rxExt\$Name\$Config\$Name.dll"),
        (Join-Path $BuildDir "rxExt\$Name\$Name.dll")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Find-OnnxRuntimeDll {
    $preferred = @(
        (Join-Path $RepoRoot "third_party\onnxruntime-win-x64-1.20.1\lib\onnxruntime.dll"),
        (Join-Path $RepoRoot "third_party\onnxruntime-win-x64-1.19.2\lib\onnxruntime.dll")
    )
    foreach ($c in $preferred) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    $root = Join-Path $RepoRoot "third_party"
    if (Test-Path $root) {
        $hit = Get-ChildItem -Path $root -Recurse -Filter "onnxruntime.dll" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'onnxruntime-win-x64' } |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
        $hit = Get-ChildItem -Path $root -Recurse -Filter "onnxruntime.dll" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

function Sync-Directory([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) { return }
    if (Test-Path $Destination) {
        Remove-Item -Path $Destination -Recurse -Force
    }
    $parent = Split-Path -Parent $Destination
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Copy-Item -Path $Source -Destination $Destination -Recurse -Force
}

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path)) {
        throw "Missing $Label : $Path"
    }
}

function Sync-OutFolder {
    Write-Host ""
    Write-Host "Syncing WIP outputs to out\ ..." -ForegroundColor Cyan

    $assetsSrc = Join-Path $RepoRoot "assets"
    $shaderSrc = Join-Path $BuildDir "shaders\spv"
    $onnx = Find-OnnxRuntimeDll
    if (-not $onnx) {
        Write-Host "WARNING: onnxruntime.dll not found under third_party\. Pixera Remover will fail to load." -ForegroundColor Yellow
        Write-Host "Re-run cmake configure so ONNX Runtime is fetched, then rebuild."
    } else {
        Write-Host "ONNX Runtime: $onnx"
    }

    # --- Editor (Debug) ---
    $editorExe = Resolve-BuiltExe "genesis_gltf_viewer.exe" $EditorConfig
    if ($editorExe) {
        $editorOut = Join-Path $OutDir "Editor"
        New-Item -ItemType Directory -Force -Path $editorOut | Out-Null
        Copy-Item -Path $editorExe -Destination (Join-Path $editorOut "genesis_gltf_viewer.exe") -Force
        Sync-Directory $assetsSrc (Join-Path $editorOut "assets")
        if (Test-Path $shaderSrc) {
            $shaderDest = Join-Path $editorOut "shaders\spv"
            New-Item -ItemType Directory -Force -Path $shaderDest | Out-Null
            Copy-Item -Path (Join-Path $shaderSrc "*.spv") -Destination $shaderDest -Force
        }
    }

    # --- Pixera scene DLL (Release) ---
    $extDll = Resolve-BuiltDll "ExtGenesis" $PixeraConfig
    if ($extDll) {
        $extOut = Join-Path $OutDir "ExtGenesis"
        New-Item -ItemType Directory -Force -Path $extOut | Out-Null
        Copy-Item -Path $extDll -Destination (Join-Path $extOut "ExtGenesis.dll") -Force
        Sync-Directory $assetsSrc (Join-Path $extOut "assets")
        Sync-Directory (Join-Path $assetsSrc "scenes") (Join-Path $extOut "scenes")
        if (Test-Path $shaderSrc) {
            $shaderDest = Join-Path $extOut "shaders\spv"
            New-Item -ItemType Directory -Force -Path $shaderDest | Out-Null
            Copy-Item -Path (Join-Path $shaderSrc "*.spv") -Destination $shaderDest -Force
        }
        Copy-Item -Path (Join-Path $RepoRoot "deploy\Genesis.manifest.json") `
            -Destination (Join-Path $extOut "Genesis.manifest.json") -Force
        Copy-Item -Path (Join-Path $RepoRoot "deploy\README-pixera.txt") `
            -Destination (Join-Path $extOut "README.txt") -Force
        if ($onnx) {
            Copy-Item -Path $onnx -Destination (Join-Path $extOut "onnxruntime.dll") -Force
        }
    }

    # --- Pixera matte-only DLL (Release) ---
    $removerDll = Resolve-BuiltDll "ExtGenesisRemover" $PixeraConfig
    if ($removerDll) {
        $remOut = Join-Path $OutDir "ExtGenesisRemover"
        New-Item -ItemType Directory -Force -Path $remOut | Out-Null
        Copy-Item -Path $removerDll -Destination (Join-Path $remOut "ExtGenesisRemover.dll") -Force
        Sync-Directory (Join-Path $assetsSrc "matting") (Join-Path $remOut "assets\matting")
        Copy-Item -Path (Join-Path $RepoRoot "deploy\GenesisRemover.manifest.json") `
            -Destination (Join-Path $remOut "GenesisRemover.manifest.json") -Force
        Copy-Item -Path (Join-Path $RepoRoot "deploy\README-remover.txt") `
            -Destination (Join-Path $remOut "README.txt") -Force
        if ($onnx) {
            Copy-Item -Path $onnx -Destination (Join-Path $remOut "onnxruntime.dll") -Force
        }

        Assert-File (Join-Path $remOut "ExtGenesisRemover.dll") "Remover DLL"
        Assert-File (Join-Path $remOut "onnxruntime.dll") "onnxruntime.dll next to Remover"
        Assert-File (Join-Path $remOut "assets\matting\rvm_mobilenetv3_fp16.onnx") "RVM model"
    }
}

function Build-Target([string]$Target, [string]$Config, [string]$LogPath) {
    Write-Host ""
    Write-Host "Building $Target ($Config) ..." -ForegroundColor Cyan
    $buildCode = Invoke-LoggedNative -LogPath $LogPath -Append -Command {
        & $CMake --build $BuildDir --config $Config --target $Target
    }
    if ($buildCode -ne 0) {
        Write-Host ""
        Write-Host "----- last errors from $LogPath -----" -ForegroundColor Red
        Select-String -Path $LogPath -Pattern "error |error:|fatal error|LNK[0-9]+|FAILED|undefined|unresolved" |
            Select-Object -Last 40 |
            ForEach-Object { $_.Line }
        Write-Host "-------------------------------------" -ForegroundColor Red
        Write-Host "Full log: $LogPath"
        throw "CMake build failed for target '$Target' ($Config) ($buildCode)"
    }
}

$CMake = Find-CMake
if (-not $CMake) {
    Write-Host ""
    Write-Host "CMake / Visual Studio C++ tools not found." -ForegroundColor Red
    Write-Host "Install Visual Studio 2022 with the 'Desktop development with C++' workload."
    Write-Host "Download: https://visualstudio.microsoft.com/downloads/"
    exit 1
}

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

$editorTargets = @()
$pixeraTargets = @()
if ($RemoverOnly) {
    $pixeraTargets = @("ExtGenesisRemover")
} elseif ($PixeraOnly) {
    $pixeraTargets = @("ExtGenesis", "ExtGenesisRemover")
} elseif ($EditorOnly) {
    $editorTargets = @("genesis_gltf_viewer")
} else {
    $editorTargets = @("genesis_gltf_viewer")
    $pixeraTargets = @("ExtGenesis", "ExtGenesisRemover")
}

Write-Host ""
Write-Host "=== GENESIS WIP build ===" -ForegroundColor Cyan
Write-Host "Repo:    $RepoRoot"
Write-Host "CMake:   $CMake"
Write-Host "Out:     $OutDir"
Write-Host "Editor:  $($editorTargets -join ', ') [$EditorConfig]"
Write-Host "Pixera:  $($pixeraTargets -join ', ') [$PixeraConfig]"
Write-Host ""

$LogFile = Join-Path $BuildDir "last-build.log"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Push-Location $RepoRoot
try {
    Write-Host "Configuring..."
    $cfgCode = Invoke-LoggedNative -LogPath $LogFile -Command {
        & $CMake -B $BuildDir -DCMAKE_BUILD_TYPE=$EditorConfig
    }
    if ($cfgCode -ne 0) { throw "CMake configure failed ($cfgCode). See $LogFile" }

    foreach ($target in $editorTargets) {
        Build-Target -Target $target -Config $EditorConfig -LogPath $LogFile
    }
    foreach ($target in $pixeraTargets) {
        Build-Target -Target $target -Config $PixeraConfig -LogPath $LogFile
    }
}
finally {
    Pop-Location
}

Sync-OutFolder

$Editor = Join-Path $OutDir "Editor\genesis_gltf_viewer.exe"
$Dll = Join-Path $OutDir "ExtGenesis\ExtGenesis.dll"
$RemoverDll = Join-Path $OutDir "ExtGenesisRemover\ExtGenesisRemover.dll"
if (-not (Test-Path $Editor)) { $Editor = $null }
if (-not (Test-Path $Dll)) { $Dll = $null }
if (-not (Test-Path $RemoverDll)) { $RemoverDll = $null }

Write-Host ""
Write-Host "=== Ready (use the out\ folder) ===" -ForegroundColor Green
Write-Host @"
out\
  Editor\                 <- run the editor from here
  ExtGenesis\             <- point PIXERA at ExtGenesis.dll
  ExtGenesisRemover\      <- point PIXERA at ExtGenesisRemover.dll (cutout only)
"@
if ($Editor) {
    Write-Host "Editor:  $Editor"
} elseif (-not $PixeraOnly -and -not $RemoverOnly) {
    Write-Host "Editor missing under out\Editor - see $LogFile" -ForegroundColor Yellow
}
if ($Dll) {
    Write-Host "Pixera:  $Dll"
} elseif (-not $EditorOnly -and -not $RemoverOnly) {
    Write-Host "ExtGenesis.dll missing under out\ExtGenesis (Windows-only)." -ForegroundColor Yellow
}
if ($RemoverDll) {
    Write-Host "Remover: $RemoverDll"
    Write-Host "In PIXERA browse to that DLL. Keep onnxruntime.dll + assets\matting next to it."
} elseif (-not $EditorOnly) {
    Write-Host "ExtGenesisRemover.dll missing under out\ExtGenesisRemover (Windows-only)." -ForegroundColor Yellow
}
Write-Host ""
Write-Host "(Ignore build\ for day-to-day testing - that is just the CMake cache.)"
Write-Host ""

if ($Run) {
    if (-not $Editor) { Write-Error "Cannot -Run: out\Editor\genesis_gltf_viewer.exe missing." }
    $EditorDir = Split-Path -Parent $Editor
    Write-Host "Launching editor from $EditorDir ..."
    Start-Process -FilePath $Editor -WorkingDirectory $EditorDir
}
