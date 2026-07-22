# AGENTS.md

## Cursor Cloud specific instructions

GENESIS is a C++20 / CMake realtime Vulkan 3D renderer for live events / virtual
production. The primary developer artifacts are the example executables built from
this repo (editor, player, headless harnesses). The PIXERA RenderBridge extension
(`ExtGenesis`) and the Inno Setup installer are **Windows-only** and are not built
on Linux (`GENESIS_BUILD_RENDERBRIDGE_EXT` is gated on `WIN32`).

### Toolchain / build (Linux, this environment)

- Build system: CMake (>=3.24) + Ninja. Dependencies (SDL3, assimp, imgui,
  ImGuizmo, fastgltf, nlohmann/json, volk, glm, spdlog, Vulkan-Headers) are pulled
  at **configure time** via `FetchContent` from `codeload.github.com` — configuring
  needs network access. See `cmake/Dependencies.cmake`.
- Shaders are compiled to SPIR-V at configure time with `glslc` (from the `glslc`
  apt package). If `glslc` is missing, configure fails early — see
  `cmake/VulkanSetup.cmake` / `cmake/CompileShaders.cmake`.
- Use **GCC**, not the default `c++`. The system `c++`/`cc` alternatives point at
  clang, which cannot find `libstdc++` here and fails the CMake compiler check.
  Always pass `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` (the update script
  already does this).
- Configure + build:
  ```bash
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
  cmake --build build            # all targets, or --target genesis_gltf_viewer etc.
  ```
- There is **no separate linter / formatter** (no `.clang-format`, `.clang-tidy`,
  pre-commit hooks, or CI in the repo). The compiler build is the effective check.

### Running (software Vulkan + virtual display)

There is no GPU. Vulkan runs via Mesa **lavapipe** (llvmpipe software ICD), and the
SDL3 window needs the XFCE virtual display on `:1`. Windowed apps must be launched
with these env vars:

```bash
export DISPLAY=:1
export XDG_RUNTIME_DIR=/tmp/xdg-runtime   # create it: mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
./build/genesis_gltf_viewer        # the editor (flagship). Optional: pass a .gltf/.glb/.genesis path
```

- `genesis_gltf_viewer` = the Scene Editor (ImGui panels + 3D viewport). `--smoke=N`
  runs N frames headlessly then exits (good for a quick render sanity check).
- Other windowed apps: `genesis_triangle`, `genesis_camera`, `genesis_player`,
  `genesis_multiview_player`, `genesis_bridge_host`.
- Headless CLI harnesses (no display/GPU needed):
  `./build/genesis_topology_validate assets/topology/examples/*.topology.json`
  and `./build/genesis_determinism_harness assets/timing/examples/basic_50hz.timing.json`.
- `Vulkan validation layers requested but not available` is a harmless warning
  (validation layers are not installed).
- The offscreen / external-shared-texture render path (used by the PIXERA bridge)
  is Windows-only; on Linux it is compiled out.

### GUI testing gotcha

The XFCE session on `:1` starts a screensaver (a rotating white cube on a black
background) after only a couple of seconds of idle, which will appear at the tail
of screen recordings once interaction stops. Keep the mouse moving during a capture
and stop the recording immediately, or trim the trailing idle frames afterward.
