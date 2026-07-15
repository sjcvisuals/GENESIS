# PIXERA integration (ExtGenesis)

GENESIS integrates with PIXERA the same way **Notch Blocks** and **TouchDesigner** do: both use the [RenderBridge extension mechanism](https://help.pixera.one/renderbridge-extension-mechanism). There are two separate pieces:

| Piece | What it is | How often |
|--------|------------|-----------|
| **Renderer** (`ExtGenesis.dll`) | RenderBridge plugin — what PIXERA's **generic extension** file picker loads | Pick the `.dll` once per layer/device setup |
| **Show** (`.gns`) | Scene + PIXERA bindings — what you author per project | Per show, like a Notch `.dfx` or TouchDesigner `.tox` |

The generic extension slot in PIXERA is **not** where you import a `.gns`. It loads `ExtGenesis.dll` (must export `rxext_open` / `rxext_close`). The show is chosen on the layer via the **`show_select`** dropdown (no path typing).

## Add GENESIS in PIXERA (generic extension)

1. **Compositing** → add a **generic RenderBridge extension** layer.
2. **Browse to `ExtGenesis.dll`** — e.g. after building:
   ```
   build\rxExt\ExtGenesis\Debug\ExtGenesis.dll
   ```
   Keep an **`assets`** folder **next to that DLL** (CMake copies it there on build) so default scenes like `scenes/demo.gns` resolve.
3. Set device **adapter LUID** (hex) to the GPU PIXERA shares with GENESIS.
4. On the layer, pick **Show** from the dropdown (no path typing).
5. Animate actors via their **per-actor parameter groups** (Position, Rotation, Scale, Color, Visible for each mesh; Intensity for each light).

You do **not** need to copy anything into `<PIXERA>/Program Files/.../data/rxExt/` for the generic extension workflow. PIXERA loads the DLL from wherever you browse.

You **do** need a proper **folder layout** next to the DLL:

```
MyGenesis/                    ← PIXERA often remembers this folder, not just the DLL
  ExtGenesis.dll
  assets/
    scenes/
      demo.gns
      demo.scene.json
```

Recommended: copy `build\rxExt\ExtGenesis\Debug\` (DLL + `assets\`) into your **PIXERA project folder** or a fixed install path, then import **that** `ExtGenesis.dll`. Avoid pointing at paths inside the GENESIS git repo unless that repo will stay put on the show machine.

After importing the DLL resource, drag it to the timeline. PIXERA should offer a **Main** stream (1920×1080). Pick **Show** from the `show_select` dropdown (e.g. `scenes/demo.gns` when using bundled assets).

### API version

PIXERA rejects extensions when `api_version` does not match its RenderBridge SDK (currently **1.2**). If loading fails with *API version mismatch*, rebuild `ExtGenesis` from the vendored `render-bridge-sdk` in this repo.

## Optional: install under PIXERA `rxExt`

Some workflows still expect extensions under:

```
<PIXERA>/data/rxExt/ExtGenesis/ExtGenesis.dll
<PIXERA>/data/rxExt/ExtGenesis/assets/
```

A GENESIS installer can register there, but the generic extension path picker is the simpler dev workflow.

## Build `ExtGenesis.dll`

```powershell
cmake --build build --config Debug --target ExtGenesis
```

Output:

```
build/rxExt/ExtGenesis/Debug/ExtGenesis.dll
build/rxExt/ExtGenesis/Debug/assets/          ← scenes, meshes, etc.
```

For local SDK testing, CMake also deploys to `third_party/render-bridge-sdk/RenderBridge/RX/modules/ExtGenesis/`.

## Layer setup (after the DLL is loaded)

## `.gns` — GENESIS show package (preferred)

`.gns` is the importable compositing resource for PIXERA, parallel to Notch `.dfx` and TouchDesigner `.tox`.

A `.gns` file bundles:

- **Scene** — inline scene document, or `scene_ref` to a sibling `.scene.json`
- **PIXERA metadata** — default stream resolution, parameter bindings (`target_node`, `light_node`), camera mode

Example (`assets/scenes/demo.gns`):

```json
{
  "format": "gns",
  "version": 1,
  "name": "Demo Scene",
  "scene_ref": "demo.scene.json",
  "pixera": {
    "streams": [
      {
        "handle": "main",
        "resolution_x": 1920,
        "resolution_y": 1080,
        "use_scene_camera": false
      }
    ],
    "bindings": {
      "target_node": "Demo Scene/Props/Sphere",
      "light_node": "Demo Scene/Directional Light"
    }
  }
}
```

When a `.gns` loads, ExtGenesis applies the embedded PIXERA bindings automatically (node paths, default camera mode, then syncs transform/color from the scene).

**Workflow:** export or author a `.gns` in GENESIS, copy it into the extension `Scenes/` folder (or PIXERA project storage), then pick it from **`show_select`**.

## Scene resources (per show)

Use **`show_select`** to choose what that layer loads:

| Value | Result |
|--------|--------|
| `scenes/demo.gns` | **Preferred** — show package with scene + PIXERA bindings |
| `demo` or `builtin` | Built-in demo scene (no `.gns` metadata) |
| `scenes/demo.scene.json` | Raw scene document (legacy) |
| Path in PIXERA storage | Resolved via `resolve_storage_filename` |
| Absolute `.gltf` / `.glb` | Direct glTF load |

When `show_select` changes, the scene reloads on the next frame. Toggle **`reload_scene`** after saving a scene in the editor to force a refresh without restarting the stream.

Bundled default assets live beside the extension DLL. **Project scenes** should live in PIXERA project storage and be referenced by path — the same idea as importing a Notch block file into the project bundle.

## What’s still missing for full Notch-like UX

Today you add a GENESIS RenderBridge layer and pick a show from `show_select`. Notch goes further: PIXERA has a dedicated **import Block** flow in Compositing that auto-creates the layer and exposes parameters from the file.

To match that for GENESIS we still need (product + code):

1. **PIXERA compositing import** for `.gns` (file picker → layer + parameters), likely with AV Stumpfl UI work or shipping GENESIS inside PIXERA like Notch.
2. **`enumerate_stream_settings` from `.gns`** so opening a show discovers streams/cameras automatically (RenderBridge API pattern).
3. **One-time installer** that registers `ExtGenesis` under `data/rxExt/`.

`.scene.json` remains the editor authoring format; `.gns` is the shipping/import format for shows.

## Manipulating the scene from PIXERA

Mesh actors and lights are **authored in the show file** (`.gns` / `.scene.json`). At extension load, GENESIS scans bundled and referenced shows and registers a parameter group per mesh actor and per light.

Each actor uses a single PIXERA folder via `group_name` (e.g. **Sphere**, **Red Cube**). Position, rotation, scale, color, and mesh visibility live inside that folder — attributes are labeled `Position / X`, `Rotation / Y`, etc.

**Scene** group:

| Parameter | Effect |
|-----------|--------|
| `show_select` | Dropdown of available shows |
| `use_scene_camera` | Use the scene output camera instead of view/proj matrices |

Example groups for the bundled demo:

| Group | Folders |
|-------|---------|
| **Sphere** | `Position` (X/Y/Z), `Rotation` (X/Y/Z), `Scale` (X/Y/Z), `Color` (0–255 RGBA), `Mesh Enabled` |
| **Red Cube** | same layout |
| **Directional Light** | `Intensity` |

Global stream parameters:

| Parameter | Purpose | Effect |
|-----------|---------|--------|
| `view_matrix` | ViewMatrix | Camera view (when not using scene camera) |
| `projection_matrix` | ProjectionMatrix | Camera projection |
| `use_scene_camera` | — | Use the scene’s output camera instead of view/proj |
| `show_select` | — | Dropdown of available shows (`.gns`, `.scene.json`, built-in demo) |
| `reload_scene` | — | Force reload after editor save (clears on next frame) |
| `position` | Position | Drives `target_node` from `.gns` bindings (for `run.lua` / keyframes) |
| `time` | TimelineTime | Timeline clock |

**Workspace gizmo:** Split X/Y/Z parameters are easier to keyframe but may not attach PIXERA’s **Show UI in Workspace** handle (that path expects purpose-tagged transform sets). Try it on a **Color** or full transform folder if PIXERA offers the option on your layer.

## Local test with RX.exe

```powershell
.\scripts\test-extgenesis.ps1
```

Or manually:

```powershell
third_party\render-bridge-sdk\RenderBridge\RX\RX.exe -script=extensions\ExtGenesis\run.lua
```

Example `run.lua` bindings:

```lua
SET_STREAM_INPUTS = function(stream)
  clock = host.create_node("SystemClock")
  stream.time = clock

  -- Animate the demo sphere upward over 5 seconds, looping
  stream.position = host.create_node("Keyframe", {
    keys = {
      { time = 0, value = { 0.15, 0.525, -0.35 } },
      { time = 5, value = { 0.15, 1.2, -0.35 } },
    },
    loop = true,
  })
end
```

To test a different show, change `show_select` in the RX layer UI or set it from Lua if your host exposes stream inputs by name.

## Architecture

```
PIXERA timeline / tracking
        ↓ per-actor parameters (actor_* / light_* groups)
ExtGenesis Input::update / render
        ↓ ActorBindings::apply (all actors every frame)
GenesisSession → SceneRuntime + OffscreenRenderer
        ↓ shared Vulkan texture
PIXERA compositor (sampler output)
```

`HostBridge` and `genesis_bridge_host` remain a **development harness** only; production shows use this extension path.

## Troubleshooting

- **Actor won't hide**: Do not use a parameter named or labeled **Visible** — PIXERA reserves that for layer visibility. Use **`Mesh Enabled`** (`actor_*_mesh_enabled`) for per-mesh show/hide. Rebuild and re-import the DLL after parameter schema changes.
- **Nothing in preview**: Preview and output use the same extension render path; both need a valid **adapter LUID** (PIXERA usually injects this per engine). Ensure layer opacity is up and **`show_select`** points at a valid show (e.g. `scenes/demo.gns`). Check PIXERA logs for `GENESIS` messages.
- **Black output**: Confirm adapter LUID matches the GPU PIXERA uses for shared textures.
- **Scene not found**: Check `show_select` value; prefer `.gns` in the extension `Scenes/` folder or PIXERA storage. If using `scene_ref`, keep the referenced scene next to the `.gns` file.
- **Transforms have no effect**: Verify `target_node` matches a node path in the loaded scene (`Parent/Child` hierarchy).
- **Wrong camera**: Enable `use_scene_camera` for scene-authored cameras, or drive `view_matrix` / `projection_matrix` from PIXERA tracking.
