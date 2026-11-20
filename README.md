# GENESIS

Realtime 3D renderer for live events, virtual production, and media server workflows.

## Install (Windows)

1. Download **`GenesisSetup-*.exe`** from [Releases](https://github.com/sjcvisuals/GENESIS/releases).
2. Run the Setup wizard.
3. Open **GENESIS** from the Start menu.

Full one-pager: [docs/install-windows.md](docs/install-windows.md).

## Requirements (using the installer)

- Windows 10/11 x64
- GPU with a Vulkan driver

## Day-to-day (developers / WIP)

You need Visual Studio 2022 (**Desktop development with C++**) and CMake.

Double-click this file in the repo root:

```
BUILD-AND-RUN.bat
```

That builds the editor (**Debug**) and Pixera DLLs (**Release** — required for PIXERA to load them), puts everything in `out\`, and launches the editor.

```
out\
  Editor\genesis_gltf_viewer.exe     <- editor (launched for you)
  ExtGenesis\ExtGenesis.dll          <- Pixera scene bridge
  ExtGenesisRemover\ExtGenesisRemover.dll  <- Pixera background cutout only
```

Ignore `build\` - that is only the CMake cache. Test from `out\` only.  
For Remover, keep `onnxruntime.dll` + `assets\matting\` next to the DLL.

In the editor: **File → Examples**.  
In PIXERA: browse to the DLL inside `out\ExtGenesis\` or `out\ExtGenesisRemover\` (keep the sibling folders next to the DLL).

Background remover details: [docs/video-bg-remover.md](docs/video-bg-remover.md).  
Pixera scene bridge: [docs/pixera-integration.md](docs/pixera-integration.md).  
Windows auto-update (WinSparkle): [docs/windows-auto-update.md](docs/windows-auto-update.md).

## Milestone status

Core vertical slice (scene runtime, editor, serialization, profiling) is complete.

### Host integration (RenderBridge path)

- [x] `ExtGenesis` — PIXERA scene / actor RenderBridge DLL
- [x] `ExtGenesisRemover` — matte-only PIXERA DLL (background cutout)
- [x] `OffscreenRenderer` + external Vulkan render target (shared texture export)
- [x] `HostBridge` — dev harness (SDL window, not the shipping PIXERA path)

### Rendering

- [x] glTF / FBX scene loading
- [x] Forward OpenPBR shading
- [ ] Shadow mapping
- [ ] HDRI / IBL

## Shipping (maintainers)

```powershell
.\scripts\package-installer.ps1
```

Primary output: `build\package\GenesisSetup-<version>.exe`  
Attach that file to the GitHub Release. Checklist: [docs/install-windows.md](docs/install-windows.md#release-checklist-maintainers).
