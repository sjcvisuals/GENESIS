# Install GENESIS on Windows

**Download → run Setup → open GENESIS.** That is the whole path.

## For everyone (recommended)

1. Open the [GENESIS Releases](https://github.com/sjcvisuals/GENESIS/releases) page.
2. Download **`GenesisSetup-*.exe`** (the Windows installer).
3. Double-click the file. If Windows asks, choose **More info → Run anyway** (unsigned builds until Authenticode signing is added).
4. Click through the wizard:
   - Welcome
   - What to install (leave **Full installation** unless you know you need less)
   - Install location (default is fine)
   - Install → Finish
5. Open **GENESIS** from the Start menu, the desktop shortcut, or the “Open GENESIS now” option on the last page.

You do **not** need Visual Studio, CMake, zip files, or DLLs by hand.

### After install

| Want to… | Do this |
|----------|---------|
| Start GENESIS | Start menu → **GENESIS** |
| Check for updates | In the Editor: **Help → Check for Updates…** |
| Uninstall | Start menu → **Uninstall GENESIS**, or Windows Apps & features |

## Optional: portable zip

Releases may also include `GENESIS-*-windows-x64-portable.zip` for advanced users who prefer not to run Setup. Unpack anywhere and run `Editor\genesis_gltf_viewer.exe`. Prefer the installer for Start menu shortcuts and clean uninstall.

---

## Release checklist (maintainers)

Cut a GitHub Release that puts Setup.exe **and** `appcast.xml` in front of users (WinSparkle reads `/releases/latest/download/appcast.xml`):

1. Bump `project(GENESIS VERSION …)` in `CMakeLists.txt` (must be **greater** than the version already installed for Check for Updates to offer it).
2. On a Windows machine (or via **Actions → Windows package**):
   ```powershell
   .\scripts\package-installer.ps1
   ```
   Output:
   - **`build\package\GenesisSetup-<version>.exe`** ← primary Release asset
   - `build\package\GENESIS-<version>-windows-x64-portable.zip` ← optional
3. Publish Release + appcast (unsigned lab OK; DSA for production):
   ```powershell
   .\scripts\publish-github-update.ps1
   ```
4. Smoke-test: install the previous version → **Help → Check for Updates…** → install → About shows the new version.

Full feed / signing / private-repo notes: [windows-auto-update.md](windows-auto-update.md).

CI artifact from `.github/workflows/windows-package.yml` is the same Setup.exe (unsigned) plus a generated `appcast.xml`. Download from the workflow run if you need a build before tagging.
