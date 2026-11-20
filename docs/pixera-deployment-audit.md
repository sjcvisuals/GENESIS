# Genesis PIXERA Deployment Audit

Phase 1 audit — current architecture, risks, and the smallest safe path to a clean extension package.

## 1. Current architecture

| Layer | CMake target | Role |
|-------|--------------|------|
| Editor | `genesis_gltf_viewer` | Scene authoring, ImGui, gizmos, save/load |
| Player | `genesis_player` | Windowed runtime verification (same scene load path as extension) |
| Dev harness | `genesis_bridge_host` | HostBridge reference loop |
| RenderBridge DLL | `ExtGenesis` | PIXERA adapter — thin RX shell over `genesis_runtime` |
| Runtime session | `genesis_runtime` | `GenesisSession`, path resolution, shared scene load |
| Engine core | `genesis_engine_runtime` | Scene, assets, glTF, GNS — no editor UI |
| Editor shell | `genesis_engine` | `Application`, `HostBridge` (windowed bootstrap) |
| Renderer | `genesis_renderer` | Windowed `Renderer` + `OffscreenRenderer` + Vulkan |
| Platform core | `genesis_platform_core` | Paths, Timer, SDL base-path |
| Platform UI | `genesis_platform` | Window, Input, ImGui, EditorPaths |

**RenderBridge flow**

```
PIXERA → rxext API → ExtGenesis.dll → GenesisSession → OffscreenRenderer → exported Vulkan texture
```

**Authoring flow**

```
Genesis Editor → .scene.json / .gns → deploy folder → PIXERA loads ExtGenesis → show_select
```

## 2. Current deployment output

After `BUILD-AND-RUN.bat` (WIP Debug):

| Output | Path |
|--------|------|
| Editor | `out/Editor/genesis_gltf_viewer.exe` |
| Pixera scene DLL | `out/ExtGenesis/ExtGenesis.dll` |
| Pixera cutout DLL | Moved to `sjcvisuals/GenesisRemover` |

`build/` is the CMake cache only — do not use it for day-to-day testing.

**`out/ExtGenesis` layout**

```
out/ExtGenesis/
├── ExtGenesis.dll
├── Genesis.manifest.json
├── README.txt
├── scenes/
├── assets/
└── shaders/spv/
```

## 3. Runtime dependencies (RenderBridge DLL)

| Dependency | Link | Required on playback | Notes |
|------------|------|----------------------|-------|
| Vulkan loader | Dynamic (`vulkan-1.dll`) | Yes | Ships with GPU driver / Windows |
| MSVC runtime | Dynamic | Yes | Standard redistributable |
| SDL3 | Static (in DLL) | No separate install | Used for Vulkan instance extensions |
| spdlog | Static | No | Logging |
| glm, fastgltf, nlohmann_json | Header/static | No | Compiled in |
| Dear ImGui | Static via renderer | Compiled in | Windowed renderer object file still linked; dead code in extension |
| Genesis Editor | Not linked | No | |

**Not required:** Vulkan SDK, CMake, Python, .NET, Genesis Editor, source tree.

## 4. RenderBridge boundary

**In ExtGenesis (PIXERA-specific)**

- `Extension.cpp`, `Device.cpp`, `Input.cpp` — RX lifecycle
- `ActorBindings`, `SceneParameterCatalog` — parameter exposure
- `ModulePaths.cpp` — DLL directory via `GetModuleFileName`
- `ResourceLoader.cpp` — thin wrapper over `runtime/RuntimePaths`

**In genesis_runtime (shared)**

- `GenesisSession` — offscreen render loop
- `RuntimePaths` — module-relative scene/asset resolution
- `RuntimeScene` — load demo/document/gltf/gns into `SceneRuntime`

**Not in extension**

- Editor panels, gizmos, undo, EditorSession
- `HostBridge`, `Application`
- ImGuizmo

## 5. Immediate risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Uncaught exceptions in `Input` constructor | High | Wrapped by `create_input_stream` try/catch; avoid new throw paths |
| Scene reload replaces active scene before validation | Medium | **Fixed:** `tryLoadResolved` loads into staging `SceneRuntime` first |
| `resolveAssetPath` used SDL exe dir not DLL dir | Medium | **Fixed:** `setRuntimeAssetRoots(module_dir)` in extension |
| Parameter catalog frozen at stream creation | Medium | Document; rebuild stream on show set change (future) |
| `.gns` `pixera.bindings` not applied | Medium | **Fixed** — `target_node` drives Position param; `light_node` syncs intensity |
| ImGui/SDL still in extension binary | Low | Accept for now; split windowed renderer later |
| Wrong GPU LUID → black frames | High | Operator must match PIXERA adapter LUID setting |

## 6. Smallest safe path (implemented)

1. **Split `genesis_engine_runtime` + `genesis_runtime`** — extension no longer links editor shell.
2. **Shared `runtime/` module** — `GenesisSession`, path resolution, scene load API.
3. **Module-relative paths** — `Scenes/`, `Assets/`, `assets/` under DLL directory.
4. **Safe scene reload** — failed load keeps previous scene.
5. **`out/` WIP package** — filled by `scripts/build.ps1` / `BUILD-AND-RUN.bat`.
6. **`genesis_player`** — validates scene load without PIXERA.
7. **Renderer split** — `genesis_renderer_core` (offscreen) vs `genesis_renderer_windowed` (editor/player); extension links core only (no ImGui / windowed `Renderer`).

## 7. Next recommended tasks

1. Clean-machine test checklist run on machine without VS/Vulkan SDK.
2. Document MSVC redistributable requirement and measure Release DLL size.
3. Rebuild parameter catalog when `show_select` changes — show list refreshes each frame; new actors register incrementally; values sync on load (restart stream if PIXERA does not pick up late parameters).

## 8. Scene format (this phase)

| Format | Use |
|--------|-----|
| `.scene.json` | Editor authoring |
| `.gns` | Show package for PIXERA (scene ref + pixera metadata) |
| `.glb` / `.gltf` | Direct mesh import |

Scene + assets deploy as a folder with relative paths. Single-file container is out of scope.

## 9. Property exposure (RenderBridge)

Stable IDs (not display names):

- `show_select` — scene selection enum
- `use_scene_camera` — bool
- `view_matrix`, `projection_matrix` — camera override
- `actor_{id}_position_x` … `actor_{id}_mesh_enabled`
- `light_{id}_intensity`
- `time` — timeline

Structural scene edits remain in Genesis Editor; save and reload in PIXERA.

## 10. Performance baseline (to measure)

Track on each release:

- ExtGenesis.dll size
- Scene load time (log in `Input::reloadSceneIfNeeded`)
- First frame after load
- Draw calls / triangles (available in editor debug panel; add extension log later)

---

*Generated as part of Genesis PIXERA deployment phase. Re-run audit when adding new linked libraries or changing the RenderBridge SDK version.*
