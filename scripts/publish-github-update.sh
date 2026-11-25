#!/usr/bin/env bash
# Attach GenesisSetup + appcast.xml to a GitHub Release (cloud/Linux helper).
# Requires: gh auth, installer file present.
set -euo pipefail

RepoRoot="$(cd "$(dirname "$0")/.." && pwd)"
Version=""
GitHubRepo="sjcvisuals/GENESIS"
InstallerPath=""
Prerelease=0
Draft=0
Title=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) Version="$2"; shift 2 ;;
    --repo) GitHubRepo="$2"; shift 2 ;;
    --installer) InstallerPath="$2"; shift 2 ;;
    --title) Title="$2"; shift 2 ;;
    --prerelease) Prerelease=1; shift ;;
    --draft) Draft=1; shift ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

command -v gh >/dev/null || { echo "gh CLI required" >&2; exit 1; }

if [[ -z "$Version" ]]; then
  Version="$(sed -nE 's/.*project\(GENESIS VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$RepoRoot/CMakeLists.txt" | head -1)"
fi
[[ -n "$Version" ]] || { echo "Could not read version" >&2; exit 1; }

if [[ -z "$InstallerPath" ]]; then
  InstallerPath="$RepoRoot/build/package/GenesisSetup-$Version.exe"
fi
[[ -f "$InstallerPath" ]] || { echo "Installer not found: $InstallerPath" >&2; exit 1; }

packageDir="$RepoRoot/build/package"
mkdir -p "$packageDir"
appcastPath="$packageDir/appcast.xml"
bash "$RepoRoot/scripts/publish-appcast.sh" --version "$Version" --repo "$GitHubRepo" --installer "$InstallerPath" --out "$appcastPath"

tag="v${Version}"
[[ -n "$Title" ]] || Title="GENESIS ${Version}"

assets=("$InstallerPath" "$appcastPath")
portable="$packageDir/GENESIS-${Version}-windows-x64-portable.zip"
[[ -f "$portable" ]] && assets+=("$portable")

notesFile="$packageDir/release-notes-${Version}.md"
cat > "$notesFile" <<EOF
## Install GENESIS

1. Download **GenesisSetup-${Version}.exe**
2. Run it and click through the wizard
3. Open **GENESIS** from the Start menu

In the Editor: **Help → Check for Updates…** uses
\`https://github.com/${GitHubRepo}/releases/latest/download/appcast.xml\`.

If Windows shows a SmartScreen warning, choose **More info → Run anyway** (unsigned until Authenticode is added).
EOF

extra=()
[[ "$Prerelease" -eq 1 ]] && extra+=(--prerelease)
[[ "$Draft" -eq 1 ]] && extra+=(--draft)

if gh release view "$tag" --repo "$GitHubRepo" >/dev/null 2>&1; then
  echo "Release $tag exists — uploading assets (clobber)."
  gh release upload "$tag" "${assets[@]}" --repo "$GitHubRepo" --clobber
else
  gh release create "$tag" "${assets[@]}" \
    --repo "$GitHubRepo" \
    --title "$Title" \
    --notes-file "$notesFile" \
    "${extra[@]}"
fi

echo ""
echo "Release: https://github.com/${GitHubRepo}/releases/tag/${tag}"
echo "Appcast: https://github.com/${GitHubRepo}/releases/latest/download/appcast.xml"
echo ""
echo "PRIVATE REPO: WinSparkle cannot auth to GitHub (assets 404 anonymously)."
echo "Make the repo public, or mirror appcast.xml + Setup.exe to public HTTPS and set GENESIS_UPDATE_FEED."
