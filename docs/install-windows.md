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

Cut a GitHub Release that puts Setup.exe in front of users:

1. Bump `project(GENESIS VERSION …)` in `CMakeLists.txt` (packaging scripts read this automatically).
2. On a Windows machine (or via **Actions → Windows package**):
   ```powershell
   .\scripts\package-installer.ps1
   ```
   Output:
   - **`build\package\GenesisSetup-<version>.exe`** ← attach this as the primary Release asset
   - `build\package\GENESIS-<version>-windows-x64-portable.zip` ← optional secondary asset
3. Create a GitHub Release (tag `v<version>`).
4. Release notes — keep them short and non-techy, for example:

   ```text
   ## Install
   1. Download GenesisSetup-<version>.exe
   2. Run it and click through the wizard
   3. Open GENESIS from the Start menu

   ## What's new
   - …
   ```

5. (Production trust, when ready) Authenticode-sign the Setup.exe; see [windows-auto-update.md](windows-auto-update.md) for WinSparkle feed signing.
6. Smoke-test on a clean Windows PC: download from the Release page → install → launch → Help → About shows the new version.

CI artifact from `.github/workflows/windows-package.yml` is the same Setup.exe (unsigned). Download it from the workflow run if you need a build before tagging.
