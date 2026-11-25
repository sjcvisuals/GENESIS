# Build a Sparkle/WinSparkle appcast for GENESIS.
# Run after packaging (or pass -InstallerPath). Does not upload by itself —
# pair with scripts/publish-github-update.ps1 / .sh to attach to a GitHub Release.
#
# Unsigned private testing: omit -PrivateKeyPath (no sparkle:dsaSignature attribute).
# Production: pass -PrivateKeyPath and bake the matching public PEM into the app
# via GENESIS_UPDATE_DSA_PEM / AutoUpdateConfig::dsa_pub_pem.
param(
    [string]$Version = "",
    [string]$GitHubRepo = "sjcvisuals/GENESIS",
    [string]$FeedBaseUrl = "",
    [string]$InstallerPath = "",
    [string]$PrivateKeyPath = "",
    [string]$OutAppcast = "",
    [string]$ReleaseNotes = ""
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

if (-not $Version) { $Version = Read-GenesisVersion }

if (-not $FeedBaseUrl) {
    # Stable WinSparkle feed: appcast.xml attached to the *latest* GitHub Release.
    $FeedBaseUrl = "https://github.com/$GitHubRepo/releases/latest/download"
}

if (-not $InstallerPath) {
    $InstallerPath = Join-Path $RepoRoot "build\package\GenesisSetup-$Version.exe"
}
if (-not (Test-Path $InstallerPath)) {
    Write-Error "Installer not found: $InstallerPath`nRun .\scripts\package-installer.ps1 first."
}

$len = (Get-Item $InstallerPath).Length
# Enclosure must point at the versioned release asset (not /latest/), so older
# clients still download the exact build advertised by the feed item.
$enclosureUrl = "https://github.com/$GitHubRepo/releases/download/v$Version/GenesisSetup-$Version.exe"
$sig = ""
$signed = $false

$signUpdate = Join-Path $RepoRoot "build\_deps\winsparkle-0.8.3\bin\sign_update.bat"
if (-not (Test-Path $signUpdate)) {
    $signUpdate = Get-ChildItem -Path (Join-Path $RepoRoot "build\_deps") -Recurse -Filter "sign_update.bat" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}

if ($PrivateKeyPath -and (Test-Path $PrivateKeyPath) -and $signUpdate) {
    Write-Host "Signing enclosure with WinSparkle sign_update..."
    $sig = & cmd /c "`"$signUpdate`" `"$InstallerPath`" `"$PrivateKeyPath`""
    $sig = ($sig | Out-String).Trim()
    if (-not $sig) { throw "sign_update produced an empty signature" }
    $signed = $true
} else {
    Write-Host "Unsigned appcast (dev/private testing). Omit sparkle:dsaSignature."
    Write-Host "For production: pass -PrivateKeyPath and set GENESIS_UPDATE_DSA_PEM in the app."
}

$pubDate = (Get-Date).ToUniversalTime().ToString("ddd, dd MMM yyyy HH:mm:ss") + " +0000"
$notesXml = ""
if ($ReleaseNotes) {
    # CDATA cannot contain "]]>" — strip if present.
    $safeNotes = $ReleaseNotes -replace ']]>', ']] >'
    $notesXml = @"

      <description><![CDATA[
$safeNotes
      ]]></description>
"@
}

$dsaAttr = ""
if ($signed) {
    $dsaAttr = "`n          sparkle:dsaSignature=`"$sig`""
}

$channelLink = "https://github.com/$GitHubRepo/releases"
$xml = @"
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <title>GENESIS</title>
    <link>$channelLink</link>
    <description>GENESIS editor updates</description>
    <language>en</language>
    <item>
      <title>GENESIS $Version</title>
      <pubDate>$pubDate</pubDate>$notesXml
      <enclosure
          url="$enclosureUrl"
          sparkle:version="$Version"
          sparkle:shortVersionString="$Version"
          sparkle:os="windows-x64"
          length="$len"
          type="application/octet-stream"$dsaAttr />
    </item>
  </channel>
</rss>
"@

if (-not $OutAppcast) {
    $OutAppcast = Join-Path $RepoRoot "build\package\appcast.xml"
}
$dir = Split-Path -Parent $OutAppcast
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
# UTF-8 without BOM — WinSparkle is picky about feed encoding.
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($OutAppcast, $xml, $utf8NoBom)

if ($FeedBaseUrl -match 'appcast\.xml$') {
    $feedUrl = $FeedBaseUrl
} else {
    $feedUrl = $FeedBaseUrl.TrimEnd('/') + "/appcast.xml"
}

Write-Host ""
Write-Host "Wrote $OutAppcast"
Write-Host "  feed URL (bake / GENESIS_UPDATE_FEED): $feedUrl"
Write-Host "  enclosure: $enclosureUrl"
Write-Host "  signed: $signed"
Write-Host "Next: attach appcast.xml + GenesisSetup-$Version.exe to GitHub Release v$Version"
Write-Host "      (see scripts/publish-github-update.ps1 or docs/windows-auto-update.md)"
