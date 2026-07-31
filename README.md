# GENESIS

Realtime 3D renderer for live events, virtual production, and media server workflows.

## Requirements

- Windows + Visual Studio 2022 (**Desktop development with C++**)
- CMake (comes with VS)
- GPU with a Vulkan driver

## Day-to-day (WIP)

Double-click this file in the repo root:

```
BUILD-AND-RUN.bat
```

That builds **Debug**, puts everything in `out\`, and launches the editor.

```
out\
  Editor\genesis_gltf_viewer.exe     <- editor (launched for you)
  ExtGenesis\ExtGenesis.dll          <- Pixera scene bridge
  ExtGenesisRemover\ExtGenesisRemover.dll  <- Pixera background cutout only
```

Ignore `build\` — that is only the CMake cache. Test from `out\` only.

In the editor: **File → Examples**.  
In PIXERA: browse to the DLL inside `out\ExtGenesis\` or `out\ExtGenesisRemover\` (keep the sibling folders next to the DLL).

Background remover details: [docs/video-bg-remover.md](docs/video-bg-remover.md).  
Pixera scene bridge: [docs/pixera-integration.md](docs/pixera-integration.md).

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

## Shipping (later)

Installer packaging is separate from WIP testing:

```powershell
.\scripts\package-installer.ps1
```

Output: `build\package\GenesisSetup-0.1.0.exe`  
The wizard can install the PIXERA extension under `<PIXERA>\data\rxExt\ExtGenesis\`.
