# Windows packaging & auto-update (WinSparkle)

GENESIS ships on Windows as **`GenesisSetup-<version>.exe`** (Inno Setup wizard) and uses **[WinSparkle](https://winsparkle.org)** for Adobe-like software updates: check feed → download → install with progress → relaunch.

**End-user install steps:** [install-windows.md](install-windows.md) (Download Setup → Run → Done).

## Versioning

| Source | Value |
|--------|--------|
| CMake `project(GENESIS VERSION …)` | `0.1.4` (see root `CMakeLists.txt`) |
| Generated `platform/Version.hpp` | `GENESIS_VERSION_STRING` |
| `scripts/package-installer.ps1` | reads version from CMakeLists.txt automatically |
| Inno `MyAppVersion` | passed as `/DMyAppVersion=` from the packaging script |
| WinSparkle `ProductVersion` | VERSIONINFO in `genesis_version.rc` + `win_sparkle_set_app_details` |

Bump the CMake `VERSION` when cutting a release; packaging picks it up. WinSparkle offers an update only when the appcast `sparkle:version` is **greater** than the installed ProductVersion.

## In-app UX (already wired)

| Action | Behavior |
|--------|----------|
| Startup | WinSparkle automatic check (interval default 24h) |
| **Help → Check for Updates…** | `win_sparkle_check_update_with_ui()` — progress / notes / install |
| **Help → About GENESIS** | Version + feed URL |

On non-Windows (or `GENESIS_UPDATE_UI_DEMO=1`), the same menu opens an ImGui **Software Update** panel that mimics check → download → install for demos.

## Default update feed (GitHub Releases)

Built-in appcast URL (CMake `GENESIS_UPDATE_APPCAST_URL`):

```text
https://github.com/sjcvisuals/GENESIS/releases/latest/download/appcast.xml
```

Each GitHub Release should attach:

| Asset | Role |
|-------|------|
| `GenesisSetup-<version>.exe` | Installer (enclosure) |
| `appcast.xml` | Sparkle feed for WinSparkle (`/latest/download/appcast.xml`) |
| `GENESIS-<version>-windows-x64-portable.zip` | Optional portable |

The enclosure inside `appcast.xml` points at the **versioned** asset:

```text
https://github.com/sjcvisuals/GENESIS/releases/download/v<version>/GenesisSetup-<version>.exe
```

Override at runtime (no rebuild): env **`GENESIS_UPDATE_FEED`**.

### Private repository blocker

`sjcvisuals/GENESIS` is **private**. Unauthenticated HTTPS to release assets returns **404**. WinSparkle has **no GitHub login**, so **Help → Check for Updates… cannot download the GitHub-hosted feed until** one of:

1. The repo (or a dedicated public updates repo) is **public**, or
2. You mirror `appcast.xml` + `GenesisSetup-*.exe` to any **public HTTPS** host and point `GENESIS_UPDATE_APPCAST_URL` / `GENESIS_UPDATE_FEED` there, or
3. For lab testing only: run `.\scripts\serve-local-update-feed.ps1` and set `GENESIS_UPDATE_FEED=http://127.0.0.1:8765/appcast.xml`.

Signed-in browsers / `gh release download` still work for humans; that does **not** help WinSparkle.

## Spencer steps (bump → package → Release → verify)

Do this on a Windows machine or via **Actions → Windows package** + the publish scripts.

### 1. Bump version

Edit root `CMakeLists.txt`:

```cmake
project(GENESIS VERSION 0.1.3 LANGUAGES CXX)   # must be > installed version
```

Commit / merge as usual.

### 2. Package

```powershell
.\scripts\package-installer.ps1
# → build\package\GenesisSetup-<version>.exe
# → build\package\GENESIS-<version>-windows-x64-portable.zip  (optional)
```

Or download the **GenesisSetup** artifact from the **Windows package** workflow.

### 3. Generate appcast + publish GitHub Release

**Unsigned (private / lab testing — works when no DSA public key is baked into the app):**

```powershell
.\scripts\publish-github-update.ps1 -Version 0.1.3
# or from Linux/cloud after the .exe is in build/package/:
# bash scripts/publish-github-update.sh --version 0.1.3
```

That writes `appcast.xml` (no `sparkle:dsaSignature`) and uploads Setup + appcast to tag `v0.1.3`.

**Production (DSA-signed enclosure):**

```powershell
# Once: from the WinSparkle zip (build\_deps\winsparkle-*\bin):
#   generate_keys.bat   → dsa_priv.pem (secret) + dsa_pub.pem
.\scripts\publish-github-update.ps1 -Version 0.1.3 -PrivateKeyPath .\keys\dsa_priv.pem
```

Bake the **public** PEM into release builds via CMake/env **`GENESIS_UPDATE_DSA_PEM`** (or `AutoUpdateConfig::dsa_pub_pem` before `AutoUpdate::init()`). Store `dsa_priv.pem` only in a secret store — never commit it.

### 4. How Spencer checks for updates

1. Install an **older** build (e.g. 0.1.1) that already has WinSparkle + Help menu.
2. Ensure the feed is reachable from that PC (public repo/mirror, or local feed — see above).
3. If the installed build still has the old placeholder feed (`updates.example.com`), set a user env var and **restart** GENESIS:
   ```bat
   setx GENESIS_UPDATE_FEED "https://github.com/sjcvisuals/GENESIS/releases/latest/download/appcast.xml"
   ```
   (Or your public mirror / `http://127.0.0.1:8765/appcast.xml`.)
4. Open GENESIS → **Help → About GENESIS** and confirm the feed URL.
5. **Help → Check for Updates…** → should offer the newer version → download → install → relaunch.
6. **Help → About** should show the new version.

0.1.2+ builds bake the GitHub `…/latest/download/appcast.xml` URL by default (still subject to the private-repo blocker).

## Signing: what works unsigned vs production trust

| Layer | Unsigned private testing | Production |
|-------|--------------------------|------------|
| WinSparkle DSA / EdDSA | **Omit** `sparkle:dsaSignature` and **do not** set `GENESIS_UPDATE_DSA_PEM` — WinSparkle accepts the feed | Generate keys; sign with `sign_update.bat`; set public PEM in the app; signed appcast required |
| Authenticode (Setup.exe) | SmartScreen “Run anyway” | Code-sign Setup.exe (separate cert/secret) |
| Feed hosting | Public HTTPS **or** local `serve-local-update-feed.ps1` | Public HTTPS (GitHub public Releases or your CDN) |

**Spencer must add for production trust**

1. **WinSparkle DSA keypair** — `dsa_priv.pem` as a secret; public PEM in builds (`GENESIS_UPDATE_DSA_PEM`).
2. **Authenticode certificate** — sign `GenesisSetup-*.exe` before upload (SmartScreen).
3. **Public feed URL** — make Releases anonymously downloadable, or host feed+installer on a public CDN and bake that URL.

## Build installer (Windows)

```powershell
.\scripts\package-installer.ps1          # Release build + stage + Inno Setup + portable zip
.\scripts\package-installer.ps1 -BuildOnly   # stage only
.\scripts\package-installer.ps1 -SkipPortableZip
```

Output (primary): `build\package\GenesisSetup-<version>.exe`  
Secondary: `build\package\GENESIS-<version>-windows-x64-portable.zip`  
Editor payload includes `WinSparkle.dll` next to `genesis_gltf_viewer.exe` when `GENESIS_ENABLE_WINSPARKLE=ON` (default on Windows).

## Helper scripts

| Script | Purpose |
|--------|---------|
| `scripts/publish-appcast.ps1` / `.sh` | Write `appcast.xml` (GitHub enclosure URLs; optional DSA) |
| `scripts/publish-github-update.ps1` / `.sh` | Create/upload GitHub Release with Setup + appcast |
| `scripts/serve-local-update-feed.ps1` | Local HTTP feed for private-repo lab tests |

## CI

GitHub Actions workflow [`.github/workflows/windows-package.yml`](../.github/workflows/windows-package.yml) builds on `windows-latest`, verifies `WinSparkle.dll`, stages Setup.exe, generates an **unsigned** `appcast.xml` artifact, and uploads both.

That artifact is **not** automatically published as a Release. Run `publish-github-update` (or upload assets by hand) after you tag.

## Cloud / Linux note

Linux cloud agents do not produce a Windows `.exe`. They can still generate `appcast.xml` and attach assets with `scripts/publish-github-update.sh` once CI has built Setup.exe. Remover / GenesisRemover is out of scope for this feed.
