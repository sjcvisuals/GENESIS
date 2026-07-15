# Host integration architecture

GENESIS separates **core rendering** from **host shells**. Two products share the same core:

| Fork | Role | Host shell |
|------|------|------------|
| **RenderBridge** (now) | PIXERA drives layers via RX extension DLL | `ExtGenesis.dll` (`extensions/ExtGenesis`) |
| **Media server** (later) | Own timeline, screen outputs, 3D-first show control | Timeline + output manager (future) |
| **Dev harness** | Local windowed testing without PIXERA | `HostBridge` + `genesis_bridge_host` |

## Layers

```
┌──────────────────────────────────────────────┐
│  PIXERA / RX engine                          │
│  - frame loop, layer parameters, textures    │
└────────────────────┬─────────────────────────┘
                     │ rxext API (Extension → StreamDevice → InputStream)
                     │ Parameters: ViewMatrix, ProjectionMatrix, TimelineTime, …
                     │ Output texture: sampler (shared Vulkan handle)
┌────────────────────▼─────────────────────────┐
│  ExtGenesis.dll                              │
│  - rxext_open / rxext_close                  │
│  - GenesisSession (SceneRuntime + OffscreenRenderer) │
└────────────────────┬─────────────────────────┘
                     │
┌────────────────────▼─────────────────────────┐
│  Core                                        │
│  SceneRuntime, Renderer, assets, shaders     │
└──────────────────────────────────────────────┘
```

## RenderBridge path (shipping)

1. Build `ExtGenesis` (CMake target, Windows only for now).
2. Deploy DLL to PIXERA `data/rxExt/ExtGenesis` or test with SDK `RX.exe` + `extensions/ExtGenesis/run.lua`.
3. Extension publishes a **shared color texture** via the `sampler` output parameter.
4. PIXERA binds **ViewMatrix**, **ProjectionMatrix**, and **TimelineTime** parameters each frame.
5. `GenesisSession` renders the demo scene (or future loaded scenes) into the external target.

SDK reference: [render-bridge-sdk](https://github.com/avstumpfl/render-bridge-sdk) (`ExtSampleVK` pattern).

## Dev harness (parallel track)

`HostBridge` + `genesis_bridge_host` simulate host-driven scene edits in an SDL window. This is **not** how PIXERA loads GENESIS — use it for local iteration only.

`genesis_gltf_viewer` remains the editor / inspector.

## Future media-server fork

Reuse:

- `SceneRuntime`, serialization, materials, lighting
- `OffscreenRenderer` / external targets for multi-output routing

Replace / add:

- Timeline scheduler
- Multi-output routing (screens, NDI, etc.)
- Show state management

Do **not** embed PIXERA `rxext` types in `scene/` or `renderer/`.

## Next integration steps

1. Scene file parameter (load `.scene.json` via `host().resolve_storage_filename()`)
2. Map PIXERA Position / Rotation / Color parameters → scene nodes
3. Input streams (PIXERA textures → GENESIS effects)
4. PIXERA deploy script / `DEPLOY_DIR` in CMake
