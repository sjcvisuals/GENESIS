# Stage binaries for the GENESIS Inno Setup installer.
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

function Copy-FilteredTree {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExcludeExtensions = @(".cpp", ".hpp", ".h")
    )
    if (-not (Test-Path $Source)) {
        throw "Missing source directory: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -Path $Source -Recurse -File | ForEach-Object {
        if ($ExcludeExtensions -contains $_.Extension.ToLowerInvariant()) {
            return
        }
        $relative = $_.FullName.Substring($Source.Length).TrimStart("\", "/")
        $target = Join-Path $Destination $relative
        $targetDir = Split-Path -Parent $target
        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
        }
        Copy-Item -Path $_.FullName -Destination $target -Force
    }
}

function Copy-Shaders {
    param([string]$DestinationRoot)
    $shaderSource = Join-Path $BuildDir "shaders\spv"
    if (-not (Test-Path $shaderSource)) {
        throw "Shader output not found: $shaderSource (build genesis_shaders first)"
    }
    $shaderDest = Join-Path $DestinationRoot "shaders\spv"
    New-Item -ItemType Directory -Force -Path $shaderDest | Out-Null
    Copy-Item -Path (Join-Path $shaderSource "*.spv") -Destination $shaderDest -Force
}

function Require-Exe {
    param([string]$Name, [string]$Path)
    if (-not $Path) {
        throw "Missing build output ($Configuration): $Name"
    }
    return $Path
}

$EditorExe = Require-Exe "genesis_gltf_viewer.exe" (Resolve-BuildPath "genesis_gltf_viewer.exe")
$PlayerExe = Require-Exe "genesis_player.exe" (Resolve-BuildPath "genesis_player.exe")
$MultiViewExe = Require-Exe "genesis_multiview_player.exe" (Resolve-BuildPath "genesis_multiview_player.exe")
$TopologyValidateExe = Require-Exe "genesis_topology_validate.exe" (Resolve-BuildPath "genesis_topology_validate.exe")
$DeterminismExe = Require-Exe "genesis_determinism_harness.exe" (Resolve-BuildPath "genesis_determinism_harness.exe")

$ExtDll = Resolve-BuildPath "rxExt\ExtGenesis\$Configuration\ExtGenesis.dll"
if (-not $ExtDll) {
    $ExtDll = Resolve-BuildPath "rxExt\ExtGenesis\ExtGenesis.dll"
}
$ExtDll = Require-Exe "ExtGenesis.dll" $ExtDll

$StageRoot = Join-Path $BuildDir "package\staging"
if (Test-Path $StageRoot) {
    Remove-Item -Path $StageRoot -Recurse -Force
}

$assetsSource = Join-Path $RepoRoot "assets"
$scenesSource = Join-Path $assetsSource "scenes"
$testAssetsSource = Join-Path $assetsSource "test"
$topologySource = Join-Path $assetsSource "topology"

# Editor (single-monitor authoring)
$editorStage = Join-Path $StageRoot "Editor"
New-Item -ItemType Directory -Force -Path $editorStage | Out-Null
Copy-Item -Path $EditorExe -Destination $editorStage -Force
Copy-Shaders -DestinationRoot $editorStage
Copy-FilteredTree -Source $assetsSource -Destination (Join-Path $editorStage "assets")
Copy-Item -Path (Join-Path $RepoRoot "deploy\README-editor.txt") -Destination (Join-Path $editorStage "README.txt") -Force

# Player (playback + topology tools — multi-output path)
$playerStage = Join-Path $StageRoot "Player"
New-Item -ItemType Directory -Force -Path $playerStage | Out-Null
Copy-Item -Path $PlayerExe -Destination $playerStage -Force
Copy-Item -Path $MultiViewExe -Destination $playerStage -Force
Copy-Item -Path $TopologyValidateExe -Destination $playerStage -Force
Copy-Item -Path $DeterminismExe -Destination $playerStage -Force
Copy-Shaders -DestinationRoot $playerStage
Copy-FilteredTree -Source $assetsSource -Destination (Join-Path $playerStage "assets")
$playerReadme = @"
Genesis Player — playback tools
================================

Tools in this folder (not the editor):

  genesis_player.exe              Standalone scene preview
  genesis_multiview_player.exe    Multi-output topology simulation
  genesis_topology_validate.exe   Validate .topology.json files
  genesis_determinism_harness.exe CPU timing/determinism replay

Example multi-view (from this folder):

  .\genesis_multiview_player.exe assets\topology\examples\one_machine_one_gpu_four_4k50.topology.json demo --frames 5

Example topology validate:

  .\genesis_topology_validate.exe assets\topology\examples\*.topology.json

The editor (single-monitor authoring) is installed separately under GENESIS\Editor.
"@
Set-Content -Path (Join-Path $playerStage "README.txt") -Value $playerReadme -Encoding UTF8

# PIXERA extension
$pixeraStage = Join-Path $StageRoot "Pixera"
New-Item -ItemType Directory -Force -Path $pixeraStage | Out-Null
Copy-Item -Path $ExtDll -Destination $pixeraStage -Force
Copy-Shaders -DestinationRoot $pixeraStage
Copy-Item -Path (Join-Path $RepoRoot "deploy\Genesis.manifest.json") -Destination $pixeraStage -Force
Copy-Item -Path (Join-Path $RepoRoot "deploy\README-pixera.txt") -Destination (Join-Path $pixeraStage "README.txt") -Force
Copy-FilteredTree -Source $scenesSource -Destination (Join-Path $pixeraStage "Scenes")
if (Test-Path $testAssetsSource) {
    Copy-FilteredTree -Source $testAssetsSource -Destination (Join-Path $pixeraStage "Assets\test")
}
if (Test-Path $topologySource) {
    Copy-FilteredTree -Source $topologySource -Destination (Join-Path $pixeraStage "Assets\topology")
}

# VC++ redistributable
$redistDir = Join-Path $StageRoot "redist"
New-Item -ItemType Directory -Force -Path $redistDir | Out-Null
$redistExe = Join-Path $redistDir "vc_redist.x64.exe"
if (-not (Test-Path $redistExe)) {
    $bundled = Join-Path $RepoRoot "installer\redist\vc_redist.x64.exe"
    if (Test-Path $bundled) {
        Copy-Item -Path $bundled -Destination $redistExe -Force
    } else {
        Write-Host "Downloading VC++ 2015-2022 redistributable..."
        $url = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
        Invoke-WebRequest -Uri $url -OutFile $redistExe -UseBasicParsing
        New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot "installer\redist") | Out-Null
        Copy-Item -Path $redistExe -Destination (Join-Path $RepoRoot "installer\redist\vc_redist.x64.exe") -Force
    }
}

Write-Host "Staged installer payload: $StageRoot"
Write-Host "  Editor:  $editorStage"
Write-Host "  Player:  $playerStage (player + multiview + topology tools)"
Write-Host "  Pixera:  $pixeraStage"
Write-Host "  Config:  $Configuration"
