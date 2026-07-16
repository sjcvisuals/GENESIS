# Distributed rendering architecture audit

Phase 1 assessment before implementing multi-view, deterministic timing, and distributed playback.

## Product boundary: editor vs player / PIXERA

| Surface | Multi-output topology | Role |
|---------|----------------------|------|
| **Editor** (`genesis_gltf_viewer`) | No | Authoring on **one monitor**. Single viewport; no production canvas partitioning. |
| **Player** (`genesis_player`, `genesis_multiview_player`) | Yes | Local simulation of machine/GPU/output assignments and large canvases. |
| **PIXERA / ExtGenesis** | Yes (target) | Referenced by PIXERA and may render assigned regions across many physical outputs, GPUs, and machines. |

Production topology, node assignment, and per-output metrics exist so **playback and host integrations** can reason about how a show is split across real hardware. The editor must not grow into a multi-output compositor — artists edit the scene; operators/hosts map it onto output topology.

## 1. Current timing model

| Path | Clock source | Used for animation? |
|------|--------------|---------------------|
| `Application` / editor | `platform/Timer` → `SDL_GetTicks()` | Yes — camera fly (`gltf_viewer`, `camera` examples) |
| `HostBridge` / player | Fixed `1/60` or manual counter | Host sets `delta_time` but **HostBridge ignores it** |
| `ExtGenesis` / PIXERA | `time` parameter (`TimelineTime`) | Passed to `GenesisSession::render()` but **discarded** |
| Scene graph | None | Transforms are pose-driven, not time-driven |

**Conclusion:** No shared deterministic production clock exists in core runtime. PIXERA owns timeline intent; Genesis does not consume it yet.

## 2. Current determinism risks

| Risk | Severity | Location |
|------|----------|----------|
| Wall-clock `deltaTime` in editor | Medium | `platform/Timer.cpp`, `Application.cpp` |
| `timeline_time` ignored in render | ~~High~~ Fixed | `runtime/GenesisSession.cpp` |
| `HostFrameInput::delta_time` unused | Medium | `host/HostBridge.cpp` |
| Fixed 60 Hz in player/bridge_host | Medium | `examples/player/main.cpp` |
| No RNG in first-party code | None | — |
| Runtime `NodeId` not serialized | Medium | Renames break paths/bindings |
| Path-based stable IDs | Good | `scene/ScenePath.cpp`, `.scene.json` |
| Per-frame filesystem scan in ExtGenesis | Low | `Input::refreshParameterCatalog()` |

**Determinism level today:** Level 1 (temporal) for host/player/PIXERA paths — production time and frame index flow through `ProductionClock`. Editor still uses wall clock. CPU determinism harness validates identical scene-state hashes across two independent clock instances.

## 3. Current viewport / output model

- **Editor:** one ImGui viewport on one monitor (`Renderer::setViewportDrawRegion()` for the 3D panel only)
- **Player / multi-view harness:** resolves topology outputs per node assignment; shared scene, sequential local renders
- **PIXERA:** one stream `"main"` hard-coded today in `Device::enumerate_stream_settings()`; topology-driven assignment is the intended next step for ExtGenesis
- **`.gns`:** `pixera.streams[]` stored; only `streams.front()` applied today
- **Single global** `active_output_camera` in scene (per-output cameras later, playback-side)

**Rule:** multi-output logic lives in Player / ExtGenesis / topology tools — not in the editor authoring loop.

## 4. Current GPU assumptions

- **One Vulkan device per session** selected by LUID (`VulkanContext::adapter_luid`)
- ExtGenesis recreates session on LUID change
- Windowed editor does not expose LUID selection
- No multi-GPU resource duplication tracking
- GPUs treated as independent devices (correct default)

## 5. RenderBridge timing capability

Available today:
- `time` → `PurposeNames::TimelineTime`
- `view_matrix`, `projection_matrix`, `use_scene_camera`
- Stream resolution from RX settings
- Adapter LUID from device/stream settings

Not wired into renderer core:
- Frame number, delta time, discontinuity flags
- Fixed simulation step
- Per-stream timing offsets from `.gns`

## 6. Current profiling capability

| Level | Editor | Player | ExtGenesis |
|-------|--------|--------|------------|
| CPU phase timing | Yes | No | No |
| Draw calls / triangles | Yes | Overlay optional | No |
| Per-GPU timing | No | No | No |
| Per-output timing | No | No | No |
| VRAM tracking | No | No | No |

`engine/FrameStats.hpp` exists; not integrated with topology budgets.

## 7. Smallest safe implementation plan

### Done (Milestone 1)

- [x] `ProductionTopology` data model (`runtime/ProductionTopology.hpp`)
- [x] JSON serialization / validation / content hash
- [x] Five example topologies under `assets/topology/examples/`
- [x] `genesis_topology_validate` CLI tool

### Done (Milestone 3 — timing foundation)

- [x] `GenesisFrameTime` + `ProductionClock` (`runtime/GenesisFrameTime.hpp`)
- [x] `HostTimingAdapter` maps host/PIXERA timing → production clock
- [x] `GenesisSession` advances production time (PIXERA `time` no longer discarded)
- [x] `HostBridge` / player / bridge_host use production time instead of wall clock
- [x] `DeterministicAnimation` test helper + `SceneStateHash`
- [x] `genesis_determinism_harness` CPU replay tool + example timing file

### Done (Milestone 2 — local multi-view)

- [x] `NodeAssignment` + `resolveOutputAssignments()` (`runtime/NodeAssignment.hpp`)
- [x] Shared-context `OffscreenRenderer` for N outputs on one GPU
- [x] `MultiViewSession` — one scene, one clock, N offscreen targets
- [x] `MultiViewHostSession` windowed harness (`host/MultiViewHostSession.hpp`)
- [x] `OutputRenderMetrics` / `MultiViewFrameMetrics` per-output timing
- [x] `genesis_multiview_player` CLI (`--machine`, `--gpu`)
- [x] Teardown fixes: depth-pipeline destruction order, no `SDL_Quit` in `Window` dtor, ImGui ini disabled for hosts

### Then (Milestone 4 — determinism harness extension)

1. Extend harness to load `.scene.json` documents and compare GPU-free scene state
2. File-based timing replay for two-process validation

### Then (Milestone 5 — node assignment)

1. CLI/env: `--machine-id`, `--gpu-id`, `--output-ids`
2. Filter topology outputs for current simulated node

### Then (Milestone 6 — PIXERA adapter)

1. Load topology assignment alongside `.gns`
2. Map host timing into `GenesisFrameTime`
3. Report topology/scene hashes in extension logs

## Out of scope (this phase)

Network sync, particle systems, scene partitioning, automatic quality reduction, cluster manager, bit-identical GPU determinism claims.

---

*Generated as part of Genesis distributed rendering phase. Re-run when adding timing, multi-view, or PIXERA cluster metadata.*
