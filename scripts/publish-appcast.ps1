# Build an installer + fill a Sparkle/WinSparkle appcast enclosure entry.
# Run on Windows after packaging. Does not upload — prints the XML snippet and
# optional DSA signature using WinSparkle tools if present.
param(
    [string]$Version = "0.1.2",
    [string]$FeedBaseUrl = "https://updates.example.com/genesis",
    [string]$InstallerPath = "",
    [string]$PrivateKeyPath = "",
    [string]$OutAppcast = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $InstallerPath) {
    $InstallerPath = Join-Path $RepoRoot "build\package\GenesisSetup-$Version.exe"
}
if (-not (Test-Path $InstallerPath)) {
    Write-Error "Installer not found: $InstallerPath`nRun .\scripts\package-installer.ps1 first (and pass -Version if needed)."
}

$len = (Get-Item $InstallerPath).Length
$url = "$FeedBaseUrl/GenesisSetup-$Version.exe"
$sig = "REPLACE_WITH_sign_update_OUTPUT"

$signUpdate = Join-Path $RepoRoot "build\_deps\winsparkle-0.8.3\bin\sign_update.bat"
if (-not (Test-Path $signUpdate)) {
    $signUpdate = Get-ChildItem -Path (Join-Path $RepoRoot "build\_deps") -Recurse -Filter "sign_update.bat" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}

if ($PrivateKeyPath -and (Test-Path $PrivateKeyPath) -and $signUpdate) {
    Write-Host "Signing enclosure with WinSparkle sign_update..."
    $sig = & cmd /c "`"$signUpdate`" `"$InstallerPath`" `"$PrivateKeyPath`""
    $sig = ($sig | Out-String).Trim()
} else {
    Write-Host "Skipping DSA signature (pass -PrivateKeyPath and ensure WinSparkle tools are fetched)."
}

$pubDate = (Get-Date).ToUniversalTime().ToString("ddd, dd MMM yyyy HH:mm:ss") + " +0000"
$xml = @"
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <title>GENESIS</title>
    <link>$FeedBaseUrl/</link>
    <description>GENESIS editor updates</description>
    <language>en</language>
    <item>
      <title>GENESIS $Version</title>
      <pubDate>$pubDate</pubDate>
      <enclosure
          url="$url"
          sparkle:version="$Version"
          sparkle:os="windows-x64"
          length="$len"
          type="application/octet-stream"
          sparkle:dsaSignature="$sig" />
    </item>
  </channel>
</rss>
"@

if (-not $OutAppcast) {
    $OutAppcast = Join-Path $RepoRoot "build\package\appcast.xml"
}
$dir = Split-Path -Parent $OutAppcast
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
Set-Content -Path $OutAppcast -Value $xml -Encoding UTF8
Write-Host ""
Write-Host "Wrote $OutAppcast"
Write-Host "Upload both:"
Write-Host "  $OutAppcast  ->  $FeedBaseUrl/appcast.xml"
Write-Host "  $InstallerPath  ->  $url"
Write-Host "Set GENESIS_UPDATE_APPCAST_URL / GENESIS_UPDATE_FEED to $FeedBaseUrl/appcast.xml"
