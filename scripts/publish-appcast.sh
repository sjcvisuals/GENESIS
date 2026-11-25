#!/usr/bin/env bash
# Generate an unsigned (or DSA-signed) WinSparkle appcast for GitHub Releases.
# Works on Linux/macOS/cloud agents once GenesisSetup-<ver>.exe is present.
set -euo pipefail

RepoRoot="$(cd "$(dirname "$0")/.." && pwd)"
Version=""
GitHubRepo="sjcvisuals/GENESIS"
InstallerPath=""
PrivateKeyPath=""
OutAppcast=""
ReleaseNotes=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) Version="$2"; shift 2 ;;
    --repo) GitHubRepo="$2"; shift 2 ;;
    --installer) InstallerPath="$2"; shift 2 ;;
    --private-key) PrivateKeyPath="$2"; shift 2 ;;
    --out) OutAppcast="$2"; shift 2 ;;
    --notes) ReleaseNotes="$2"; shift 2 ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$Version" ]]; then
  Version="$(sed -nE 's/.*project\(GENESIS VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$RepoRoot/CMakeLists.txt" | head -1)"
fi
[[ -n "$Version" ]] || { echo "Could not read version from CMakeLists.txt" >&2; exit 1; }

if [[ -z "$InstallerPath" ]]; then
  InstallerPath="$RepoRoot/build/package/GenesisSetup-$Version.exe"
fi
[[ -f "$InstallerPath" ]] || { echo "Installer not found: $InstallerPath" >&2; exit 1; }

if [[ -z "$OutAppcast" ]]; then
  OutAppcast="$RepoRoot/build/package/appcast.xml"
fi
mkdir -p "$(dirname "$OutAppcast")"

len="$(wc -c < "$InstallerPath" | tr -d ' ')"
enclosureUrl="https://github.com/${GitHubRepo}/releases/download/v${Version}/GenesisSetup-${Version}.exe"
pubDate="$(date -u '+%a, %d %b %Y %H:%M:%S +0000')"
channelLink="https://github.com/${GitHubRepo}/releases"

dsaAttr=""
if [[ -n "$PrivateKeyPath" && -f "$PrivateKeyPath" ]]; then
  if command -v openssl >/dev/null 2>&1; then
    # WinSparkle sign_update is Windows-only; openssl DSA path is documented for Sparkle.
    # Prefer running publish-appcast.ps1 on Windows for production signatures.
    echo "WARNING: DSA signing from this shell is best-effort; prefer WinSparkle sign_update.bat on Windows." >&2
  fi
  echo "Pass signatures from Windows sign_update.bat for production. Generating unsigned feed." >&2
fi

notesBlock=""
if [[ -n "$ReleaseNotes" ]]; then
  safeNotes="${ReleaseNotes//]]>/]] >}"
  notesBlock=$(printf '\n      <description><![CDATA[\n%s\n      ]]></description>' "$safeNotes")
fi

cat > "$OutAppcast" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <title>GENESIS</title>
    <link>${channelLink}</link>
    <description>GENESIS editor updates</description>
    <language>en</language>
    <item>
      <title>GENESIS ${Version}</title>
      <pubDate>${pubDate}</pubDate>${notesBlock}
      <enclosure
          url="${enclosureUrl}"
          sparkle:version="${Version}"
          sparkle:shortVersionString="${Version}"
          sparkle:os="windows-x64"
          length="${len}"
          type="application/octet-stream"${dsaAttr} />
    </item>
  </channel>
</rss>
EOF

feedUrl="https://github.com/${GitHubRepo}/releases/latest/download/appcast.xml"
echo "Wrote $OutAppcast"
echo "  feed URL: $feedUrl"
echo "  enclosure: $enclosureUrl"
echo "  signed: no (unsigned OK when the app has no GENESIS_UPDATE_DSA_PEM)"
