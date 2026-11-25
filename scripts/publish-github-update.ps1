# Publish GenesisSetup + appcast.xml to a GitHub Release so WinSparkle can find updates.
# Requires: gh auth, packaged installer at build\package\GenesisSetup-<ver>.exe
#
# Example (unsigned private/testing feed):
#   .\scripts\package-installer.ps1
#   .\scripts\publish-github-update.ps1 -Version 0.1.2
#
# Production (DSA-signed enclosure):
#   .\scripts\publish-github-update.ps1 -Version 0.1.2 -PrivateKeyPath .\keys\dsa_priv.pem
param(
    [string]$Version = "",
    [string]$GitHubRepo = "sjcvisuals/GENESIS",
    [string]$InstallerPath = "",
    [string]$PrivateKeyPath = "",
    [string]$Title = "",
    [string]$NotesFile = "",
    [switch]$Prerelease,
    [switch]$Draft,
    [switch]$SkipAppcast
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Read-GenesisVersion {
    $cmake = Join-Path $RepoRoot "CMakeLists.txt"
    $match = Select-String -Path $cmake -Pattern 'project\s*\(\s*GENESIS\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
        Select-Object -First 1
    if (-not $match) { throw "Could not read GENESIS VERSION from CMakeLists.txt" }
    return $match.Matches[0].Groups[1].Value
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI (gh) is required. Install from https://cli.github.com/"
}

if (-not $Version) { $Version = Read-GenesisVersion }
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $RepoRoot "build\package\GenesisSetup-$Version.exe"
}
if (-not (Test-Path $InstallerPath)) {
    throw "Installer not found: $InstallerPath — run .\scripts\package-installer.ps1 first."
}

$packageDir = Join-Path $RepoRoot "build\package"
$appcastPath = Join-Path $packageDir "appcast.xml"
$tag = "v$Version"
if (-not $Title) { $Title = "GENESIS $Version" }

if (-not $SkipAppcast) {
    $pubArgs = @{
        Version        = $Version
        GitHubRepo     = $GitHubRepo
        InstallerPath  = $InstallerPath
        OutAppcast     = $appcastPath
    }
    if ($PrivateKeyPath) { $pubArgs.PrivateKeyPath = $PrivateKeyPath }
    & (Join-Path $PSScriptRoot "publish-appcast.ps1") @pubArgs
    if (-not (Test-Path $appcastPath)) { throw "appcast.xml was not written" }
}

$assets = @($InstallerPath)
$portable = Join-Path $packageDir "GENESIS-$Version-windows-x64-portable.zip"
if (Test-Path $portable) { $assets += $portable }
if (Test-Path $appcastPath) { $assets += $appcastPath }

$notesArgs = @()
if ($NotesFile -and (Test-Path $NotesFile)) {
    $notesArgs += "--notes-file", $NotesFile
} else {
    $defaultNotes = @"
## Install GENESIS

1. Download **GenesisSetup-$Version.exe**
2. Run it and click through the wizard
3. Open **GENESIS** from the Start menu

In the Editor: **Help → Check for Updates…** uses the WinSparkle feed
``https://github.com/$GitHubRepo/releases/latest/download/appcast.xml``.

If Windows shows a SmartScreen warning, choose **More info → Run anyway** (unsigned until Authenticode is added).
"@
    $tmpNotes = Join-Path $packageDir "release-notes-$Version.md"
    Set-Content -Path $tmpNotes -Value $defaultNotes -Encoding UTF8
    $notesArgs += "--notes-file", $tmpNotes
}

$extra = @()
if ($Prerelease) { $extra += "--prerelease" }
if ($Draft) { $extra += "--draft" }

Write-Host "Creating/updating GitHub Release $tag on $GitHubRepo ..."
# Prefer create; if the release exists, upload/clobber assets.
$existing = gh release view $tag --repo $GitHubRepo 2>$null
if ($LASTEXITCODE -eq 0 -and $existing) {
    Write-Host "Release $tag already exists — uploading assets (clobber)."
    gh release upload $tag @assets --repo $GitHubRepo --clobber
} else {
    gh release create $tag @assets `
        --repo $GitHubRepo `
        --title $Title `
        @notesArgs `
        @extra
}

$feed = "https://github.com/$GitHubRepo/releases/latest/download/appcast.xml"
Write-Host ""
Write-Host "Done."
Write-Host "  Release: https://github.com/$GitHubRepo/releases/tag/$tag"
Write-Host "  Appcast: $feed"
Write-Host "  Bake GENESIS_UPDATE_APPCAST_URL=$feed (already the CMake default)."
Write-Host ""
Write-Host "PRIVATE REPO NOTE: WinSparkle cannot authenticate to GitHub. Unauthenticated"
Write-Host "downloads of release assets return 404. For Check for Updates to work the feed"
Write-Host "and GenesisSetup must be on a publicly reachable HTTPS URL (public repo, or"
Write-Host "mirror the two files elsewhere and set GENESIS_UPDATE_FEED)."
