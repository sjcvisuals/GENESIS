# Local HTTPS-less update feed for private-repo / lab testing.
# Copies (or links) GenesisSetup + generates appcast with http://127.0.0.1:<port>/ URLs,
# then serves the folder. Point the installed app at it:
#
#   setx GENESIS_UPDATE_FEED "http://127.0.0.1:8765/appcast.xml"
#   # restart GENESIS, then Help → Check for Updates…
#
# WinSparkle accepts http on localhost; production feeds must be HTTPS.
param(
    [string]$Version = "",
    [string]$InstallerPath = "",
    [int]$Port = 8765,
    [string]$ListenAddress = "127.0.0.1"
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
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $RepoRoot "build\package\GenesisSetup-$Version.exe"
}
if (-not (Test-Path $InstallerPath)) {
    throw "Installer not found: $InstallerPath"
}

$feedDir = Join-Path $RepoRoot "build\package\local-feed"
New-Item -ItemType Directory -Force -Path $feedDir | Out-Null
$destSetup = Join-Path $feedDir "GenesisSetup-$Version.exe"
Copy-Item -Path $InstallerPath -Destination $destSetup -Force

$len = (Get-Item $destSetup).Length
$base = "http://${ListenAddress}:$Port"
$enclosureUrl = "$base/GenesisSetup-$Version.exe"
$pubDate = (Get-Date).ToUniversalTime().ToString("ddd, dd MMM yyyy HH:mm:ss") + " +0000"

$xml = @"
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <title>GENESIS (local test feed)</title>
    <link>$base/</link>
    <description>Unsigned local feed for private testing</description>
    <language>en</language>
    <item>
      <title>GENESIS $Version</title>
      <pubDate>$pubDate</pubDate>
      <enclosure
          url="$enclosureUrl"
          sparkle:version="$Version"
          sparkle:shortVersionString="$Version"
          sparkle:os="windows-x64"
          length="$len"
          type="application/octet-stream" />
    </item>
  </channel>
</rss>
"@
$appcast = Join-Path $feedDir "appcast.xml"
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($appcast, $xml, $utf8NoBom)

Write-Host "Local feed ready in $feedDir"
Write-Host "  GENESIS_UPDATE_FEED=$base/appcast.xml"
Write-Host "Serving on $base  (Ctrl+C to stop)"
Write-Host ""

# Tiny static file server (no admin / no IIS).
$listener = New-Object System.Net.HttpListener
$prefix = "http://${ListenAddress}:$Port/"
$listener.Prefixes.Add($prefix)
$listener.Start()
try {
    while ($listener.IsListening) {
        $ctx = $listener.GetContext()
        $path = $ctx.Request.Url.LocalPath.TrimStart('/')
        if ([string]::IsNullOrWhiteSpace($path)) { $path = "appcast.xml" }
        $file = Join-Path $feedDir $path
        if (Test-Path $file) {
            $bytes = [System.IO.File]::ReadAllBytes($file)
            $ctx.Response.StatusCode = 200
            if ($path -like "*.xml") {
                $ctx.Response.ContentType = "application/xml; charset=utf-8"
            } else {
                $ctx.Response.ContentType = "application/octet-stream"
            }
            $ctx.Response.ContentLength64 = $bytes.Length
            $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $ctx.Response.StatusCode = 404
        }
        $ctx.Response.OutputStream.Close()
    }
} finally {
    $listener.Stop()
}
