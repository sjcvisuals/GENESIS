# Windows packaging & auto-update (WinSparkle)

GENESIS ships on Windows as **`GenesisSetup-<version>.exe`** (Inno Setup wizard) and uses **[WinSparkle](https://winsparkle.org)** for Adobe-like software updates: check feed → download → install with progress → relaunch.

**End-user install steps:** [install-windows.md](install-windows.md) (Download Setup → Run → Done).

## Versioning

| Source | Value |
|--------|--------|
| CMake `project(GENESIS VERSION …)` | `0.1.1` (see root `CMakeLists.txt`) |
| Generated `platform/Version.hpp` | `GENESIS_VERSION_STRING` |
| `scripts/package-installer.ps1` | reads version from CMakeLists.txt automatically |
| Inno `MyAppVersion` | passed as `/DMyAppVersion=` from the packaging script |
| WinSparkle `ProductVersion` | VERSIONINFO in `genesis_version.rc` + `win_sparkle_set_app_details` |

Bump the CMake `VERSION` when cutting a release; packaging picks it up.

## Build installer (Windows)

```powershell
.\scripts\package-installer.ps1          # Release build + stage + Inno Setup + portable zip
.\scripts\package-installer.ps1 -BuildOnly   # stage only
.\scripts\package-installer.ps1 -SkipPortableZip
```

Output (primary): `build\package\GenesisSetup-<version>.exe`  
Secondary: `build\package\GENESIS-<version>-windows-x64-portable.zip`  
Editor payload includes `WinSparkle.dll` next to `genesis_gltf_viewer.exe` when `GENESIS_ENABLE_WINSPARKLE=ON` (default on Windows).

## Update feed (appcast)

1. Host HTTPS files, e.g.:
   - `https://updates.example.com/genesis/appcast.xml`
   - `https://updates.example.com/genesis/GenesisSetup-0.1.2.exe`
2. Start from `installer/appcast.example.xml`.
3. Sign the installer with WinSparkle’s tools (`generate_keys.bat` / `sign_update.bat` from the downloaded WinSparkle package) and put the DSA signature on the `<enclosure>`.
4. Embed the **DSA public key** in the app:
   - Env `GENESIS_UPDATE_DSA_PEM` (PEM text), or
   - `AutoUpdateConfig::dsa_pub_pem` before `AutoUpdate::init()`.
5. Point the feed URL with CMake `GENESIS_UPDATE_APPCAST_URL` or runtime env **`GENESIS_UPDATE_FEED`**.

Helper script (does not upload):

```powershell
.\scripts\publish-appcast.ps1 -Version 0.1.2 -FeedBaseUrl https://updates.example.com/genesis `
  -PrivateKeyPath .\keys\dsa_priv.pem
```

## In-app UX

| Action | Behavior |
|--------|----------|
| Startup | WinSparkle automatic check (interval default 24h) |
| **Help → Check for Updates…** | `win_sparkle_check_update_with_ui()` — progress / notes / install |
| **Help → About GENESIS** | Version + feed URL |

On non-Windows (or `GENESIS_UPDATE_UI_DEMO=1`), the same menu opens an ImGui **Software Update** panel that mimics check → download → install for demos.

## Signed release checklist

1. Code-sign `GenesisSetup-*.exe` (Authenticode) for SmartScreen trust.
2. DSA-sign the enclosure for WinSparkle (separate from Authenticode).
3. Publish appcast + installer to the HTTPS feed.
4. Verify a machine on the previous version offers the update and relaunches cleanly.

## CI

GitHub Actions workflow [`.github/workflows/windows-package.yml`](../.github/workflows/windows-package.yml) builds on `windows-latest` (Vulkan SDK via `humbletim/install-vulkan-sdk@v1.2`), verifies `WinSparkle.dll`, stages the installer tree, and (when Inno Setup installs via Chocolatey) uploads `GenesisSetup-*.exe` as an artifact.

That artifact is **not** a signed release. Production still needs:

- Authenticode signing of the installer
- WinSparkle DSA keys + signed appcast hosted on HTTPS
- `GENESIS_UPDATE_APPCAST_URL` / `GENESIS_UPDATE_DSA_PEM` baked or injected at release time

## Cloud / Linux note

Linux cloud agents do not produce a Windows `.exe`. Packaging scripts, WinSparkle CMake fetch, VERSIONINFO, appcast template, updater wiring, and the Windows package workflow land in-repo so a Windows CI or developer machine can cut a signed release.
