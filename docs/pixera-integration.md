# PIXERA integration (ExtGenesis)

GENESIS integrates with PIXERA the same way **Notch Blocks** and **TouchDesigner** do: both use the [RenderBridge extension mechanism](https://help.pixera.one/renderbridge-extension-mechanism). There are two separate pieces:

| Piece | What it is | How often |
|--------|------------|-----------|
| **Renderer** (`ExtGenesis.dll`) | RenderBridge plugin — what PIXERA's **generic extension** file picker loads | Pick the `.dll` once per layer/device setup |
| **Show** (`.gns`) | Scene + PIXERA bindings — what you author per project | Per show, like a Notch `.dfx` or TouchDesigner `.tox` |

The generic extension slot in PIXERA is **not** where you import a `.gns` / `.genesis`. It loads `ExtGenesis.dll` (must export `rxext_open` / `rxext_close`). Packaged shows (`.gns` / `.genesis`) are discovered from folders **next to the DLL** and chosen with the layer **Scene** dropdown (Notch-like: launch + scene pick; expose-from-Genesis comes later).

### Editor vs Player / PIXERA outputs

- **Editor** — author scenes on a single monitor. It does not model multi-GPU or multi-output canvases.
- **Player / ExtGenesis** — where production topology applies. PIXERA may drive Genesis across large resolutions, multiple physical outputs, and eventually multiple machines/GPUs. Local tools (`genesis_multiview_player`, topology JSON) exist to prove that playback path before full PIXERA wiring.

## Add GENESIS in PIXERA (generic extension)

1. **Compositing** → add a **generic RenderBridge extension** layer.
2. **Browse to `ExtGenesis.dll`** — after double-clicking `BUILD-AND-RUN.bat`:
   ```
   out\ExtGenesis\ExtGenesis.dll
   ```
   Keep **`scenes/`** and **`assets/`** next to that DLL. Drop project shows into `scenes/`.
3. Set device **adapter LUID** (hex) to the GPU PIXERA shares with GENESIS.
4. On the layer, pick **Scene** from the dropdown (packaged `.gns` / `.genesis` under `scenes/` and `assets/scenes/`).
5. Author actors/lights in the GENESIS editor and save the packaged show — the clip does **not** dump every transform into PIXERA (expose-from-Genesis is deferred).

You do **not** need to copy anything into `<PIXERA>/Program Files/.../data/rxExt/` for the generic extension workflow. PIXERA loads the DLL from wherever you browse.

You **do** need a proper **folder layout** next to the DLL:

```
MyGenesis/                    ← PIXERA often remembers this folder, not just the DLL
  ExtGenesis.dll
  scenes/                     ← drop your .genesis / .gns shows here (preferred)
    MyShow.genesis
  assets/
    scenes/                   ← bundled defaults (also scanned into the dropdown)
      default.gns
      default.scene.json
```

Recommended: copy `out\ExtGenesis\` (DLL + `scenes\` + `assets\`) into your **PIXERA project folder** or a fixed install path, then import **that** `ExtGenesis.dll`. Avoid pointing at paths inside the GENESIS git repo unless that repo will stay put on the show machine.

After importing the DLL resource, drag it to the timeline. PIXERA should offer a **Main** stream (1920×1080). Pick a show from the **Show** dropdown — entries come from `scenes/` and `assets/scenes/` beside the DLL.

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

Double-click `BUILD-AND-RUN.bat` (or `.\scripts\build.ps1`). Output for PIXERA:

```
out/ExtGenesis/ExtGenesis.dll
out/ExtGenesis/assets/
out/ExtGenesis/scenes/
```

Matte-only cutout module (no scenes): see [`sjcvisuals/GenesisRemover`](https://github.com/sjcvisuals/GenesisRemover).

## Layer setup (after the DLL is loaded)

## `.gns` — GENESIS show package (preferred)

`.gns` is the importable compositing resource for PIXERA, parallel to Notch `.dfx` and TouchDesigner `.tox`.

A `.gns` file bundles:

- **Scene** — inline scene document, or `scene_ref` to a sibling `.scene.json`
- **PIXERA metadata** — default stream resolution, parameter bindings (`target_node`, `light_node`), camera mode

Example (`assets/scenes/default.gns`):

```json
{
  "format": "gns",
  "version": 1,
  "name": "Default Scene",
  "scene_ref": "default.scene.json",
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
      "target_node": "Scene/Cube",
      "light_node": "Scene/Directional Light"
    }
  }
}
```

When a `.gns` loads, ExtGenesis applies the embedded PIXERA bindings automatically (node paths, default camera mode, then syncs transform/color from the scene).

**Workflow:** save a `.genesis` or `.gns` from the GENESIS editor into the extension’s **`scenes/`** folder (next to `ExtGenesis.dll`), then pick it from the layer **Scene** dropdown. Bundled packaged shows under `assets/scenes/` are scanned too.

## Scene resources (per show)

ExtGenesis scans these folders next to the DLL and fills **`show_select`**:

| Folder | Role |
|--------|------|
| `scenes/` | Project shows you drop next to the DLL (preferred) |
| `assets/scenes/` | Bundled defaults shipped with the module |

Dropdown lists packaged shows only: `.genesis`, `.gns`. Authoring `.scene.json` / glTF still resolve if selected by resource string, but are not listed in the clip UI.

| Dropdown / resolve value | Result |
|--------|--------|
| `scenes/MyShow.genesis` | Load from `scenes/` beside the DLL |
| `assets/scenes/default.gns` | Bundled default package |
| `default`, `demo`, or `builtin` | Built-in cube + directional light (no package metadata) |

When **Scene** changes, the show reloads on the next frame. Toggle **`reload_scene`** after saving in the editor (and copying into `scenes/`) to force a refresh without restarting the stream.

Bundled default assets live beside the extension DLL. **Project scenes** should live in PIXERA project storage and be referenced by path — the same idea as importing a Notch block file into the project bundle.

## What’s still missing for full Notch-like UX

Today you add a GENESIS RenderBridge layer and pick a show from the **Show** dropdown (files under `scenes/` / `assets/scenes/`). Notch goes further: PIXERA has a dedicated **import Block** flow in Compositing that auto-creates the layer and exposes parameters from the file.

To match that for GENESIS we still need (product + code):

1. **PIXERA compositing import** for `.gns` (file picker → layer + parameters), likely with AV Stumpfl UI work or shipping GENESIS inside PIXERA like Notch.
2. **`enumerate_stream_settings` from `.gns`** so opening a show discovers streams/cameras automatically (RenderBridge API pattern).
3. **One-time installer** that registers `ExtGenesis` under `data/rxExt/`.

`.scene.json` remains the editor authoring format; `.gns` is the shipping/import format for shows.

## Manipulating the scene from PIXERA

Mesh actors and lights are **authored in the show file** (`.gns` / `.scene.json`) in the GENESIS editor. The ExtGenesis clip stays Notch-like: pick a packaged scene and play it. Per-actor/light parameter exposure (**expose-from-Genesis**) is deferred — the clip does not register Position/Rotation/Scale/Color folders for every mesh.

**Scene** group:

| Parameter | Effect |
|-----------|--------|
| `show_select` | Dropdown of packaged shows (`.gns` / `.genesis`) in `scenes/` and `assets/scenes/` |
| `use_scene_camera` | Use the scene output camera instead of view/proj matrices |
| `reload_scene` | Force reload after editor save (clears on next frame) |

Global stream parameters:

| Parameter | Purpose | Effect |
|-----------|---------|--------|
| `view_matrix` | ViewMatrix | Camera view (when not using scene camera) |
| `projection_matrix` | ProjectionMatrix | Camera projection |
| `use_scene_camera` | — | Use the scene’s output camera instead of view/proj |
| `show_select` | — | Dropdown of packaged shows from `scenes/` and `assets/scenes/` |
| `reload_scene` | — | Force reload after editor save (clears on next frame) |
| `time` | TimelineTime | Timeline clock |

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

To test a different show, copy a `.gns` / `.genesis` into `scenes/` next to the DLL and pick it from **Scene** (or toggle **Reload Scene**).

## Architecture

```
PIXERA timeline / tracking
        ↓ show_select (Scene dropdown) + camera matrices / time
ExtGenesis Input::update / render
        ↓ load packaged show → tick / render
GenesisSession → SceneRuntime + OffscreenRenderer
        ↓ shared Vulkan texture
PIXERA compositor (sampler output)
```

`HostBridge` and `genesis_bridge_host` remain a **development harness** only; production shows use this extension path.

## Troubleshooting

- **Nothing in preview**: Preview and output use the same extension render path; both need a valid **adapter LUID** (PIXERA usually injects this per engine). Ensure layer opacity is up and **Scene** points at a valid packaged show. Check PIXERA logs for `GENESIS` messages.
- **Black output**: Confirm adapter LUID matches the GPU PIXERA uses for shared textures.
- **Scene not found**: Put the `.genesis` / `.gns` under `scenes/` (or `assets/scenes/`) next to `ExtGenesis.dll`, then pick it from **Scene**. If using `scene_ref`, keep the referenced scene next to the package file. Authoring `.scene.json` alone is not listed in the dropdown.
- **Wrong camera**: Enable `use_scene_camera` for scene-authored cameras, or drive `view_matrix` / `projection_matrix` from PIXERA tracking.
- **Missing actor params in PIXERA**: Expected for now — author in GENESIS and reload the packaged show. Expose-from-Genesis comes later.
