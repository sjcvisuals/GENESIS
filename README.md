# GENESIS

Realtime 3D renderer for live events, virtual production, and media server workflows.

## Requirements

- CMake 3.24+
- C++20 compiler (MSVC 2022, Clang 17+, GCC 13+)
- [Vulkan SDK](https://vulkan.lunarg.com/) with `VULKAN_SDK` set
- Git (for FetchContent dependencies)

## Build

**Easiest path (Windows):** double-click or run:

```powershell
.\scripts\build-and-run.bat
```

That builds the editor + Pixera DLLs (scene + background remover), prints the exact paths, and launches the editor.
Then use **File → Examples**.

Or from PowerShell in the repo root:

```powershell
.\scripts\build.ps1              # build only (prints paths)
.\scripts\build.ps1 -Run         # build + launch editor
.\scripts\build.ps1 -RemoverOnly # background-cutout DLL only
.\scripts\build.ps1 -Config Debug
```

Requirements: Visual Studio 2022 with the **Desktop development with C++** workload.
The script will try to fetch `glslc` / Vulkan runtime if needed.

**Manual build:**

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target genesis_gltf_viewer ExtGenesis ExtGenesisRemover
```

Dependencies are fetched automatically via CMake.

## Run

After `.\scripts\build.ps1`, the script prints the real paths. Typically:

```
build\Release\genesis_gltf_viewer.exe                        # editor
build\rxExt\ExtGenesis\Release\ExtGenesis.dll                # Pixera scenes
build\rxExt\ExtGenesisRemover\Release\ExtGenesisRemover.dll  # Pixera matte-only
build\deploy\GenesisRemover\                                 # ready-to-copy Remover package
```

(MSVC Debug builds use `Debug\` instead of `Release\`.)

In the editor: **File → Examples**.  
In PIXERA (scenes): browse to `ExtGenesis.dll` and keep sibling `assets\` / `scenes\`.  
In PIXERA (cutout only): browse to `ExtGenesisRemover.dll` and keep sibling `assets\matting\` (+ `onnxruntime.dll`). See [docs/video-bg-remover.md](docs/video-bg-remover.md).

## Milestone status

Core vertical slice (scene runtime, editor, serialization, profiling) is complete.

### Host integration (RenderBridge path)

- [x] `ExtGenesis` — PIXERA RenderBridge extension DLL (`rxext` API)
- [x] `ExtGenesisRemover` — matte-only PIXERA DLL (background cutout, no scenes)
- [x] `OffscreenRenderer` + external Vulkan render target (shared texture export)
- [x] `HostBridge` — dev harness (SDL window, not the shipping PIXERA path)
- [x] `genesis_bridge_host` — reference dev integration example
- [x] Scene load via PIXERA parameters / storage paths
- [x] Map PIXERA Position / Rotation / Color → scene nodes

See [docs/pixera-integration.md](docs/pixera-integration.md) for PIXERA setup and parameter binding.

### Rendering

- [x] glTF scene + mesh loading (first mesh)
- [x] Forward lighting + material factors
- [x] Cook-Torrance GGX PBR (metallic-roughness factors, no textures yet)
- [ ] glTF textures (GPU sampling)
- [ ] Multi-mesh glTF primitives
- [ ] Shadow mapping
- [ ] HDRI / IBL

See `docs/host-bridge.md` for fork architecture.

## Windows installer

Build a single setup executable with component selection (Editor / Player / PIXERA extension):

1. Install [Inno Setup 6](https://jrsoftware.org/isinfo.php)
2. Run:

```powershell
.\scripts\package-installer.ps1
```

Output: `build\package\GenesisSetup-0.1.1.exe`

The wizard lets users pick which components to install. The PIXERA extension is copied to `<PIXERA>\data\rxExt\ExtGenesis\`. Uninstall via Windows Settings or the GENESIS uninstaller entry.

## Genesis Remover (free PIXERA cutout)

Standalone matte-only fork for PIXERA — realtime person cutout, no scenes / editor.
Drag SDI / NDI / media onto **Video In**; output is a transparent RGBA cutout.

```powershell
.\scripts\package-remover-installer.ps1
```

Free download artifacts:

- `build\package\GenesisRemoverSetup-0.1.0.exe` — Windows wizard (installs into PIXERA `rxExt`)
- `build\package\GenesisRemover-0.1.0.zip` — portable folder for the generic extension picker

End-user steps: [docs/genesis-remover-install.md](docs/genesis-remover-install.md).

## Project layout

See `docs/architecture.md` for system design.
