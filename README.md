# GENESIS

Realtime 3D renderer for live events, virtual production, and media server workflows.

## Requirements

- CMake 3.24+
- C++20 compiler (MSVC 2022, Clang 17+, GCC 13+)
- [Vulkan SDK](https://vulkan.lunarg.com/) with `VULKAN_SDK` set
- Git (for FetchContent dependencies)

## Build

**Quick start (Windows):**

```powershell
.\scripts\build.ps1
```

This script provisions `glslc` from the Vulkan SDK installer (no admin required), installs the Vulkan runtime if needed, configures, and builds.

**Manual build:**

```powershell
$env:VULKAN_SDK = "$env:LOCALAPPDATA\VulkanSDK\1.4.350.0"   # after glslc is extracted
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

Dependencies are fetched automatically via CMake (no Git required).

## Run

```bash
./build/genesis_triangle      # Linux/macOS
build\Debug\genesis_triangle.exe  # Windows MSVC
```

Other examples:

- `build\\Debug\\genesis_camera.exe` (WASD + RMB look)

## Milestone status

Core vertical slice (scene runtime, editor, serialization, profiling) is complete.

### Host integration (RenderBridge path)

- [x] `ExtGenesis` — PIXERA RenderBridge extension DLL (`rxext` API)
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

## Project layout

See `docs/architecture.md` for system design.
