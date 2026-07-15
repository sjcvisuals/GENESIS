# PIXERA clean-machine verification checklist

Run on a Windows machine **without** Visual Studio, Vulkan SDK, or the GENESIS source tree.

## Package under test

Copy `build/deploy/GenesisPixera/` (or a Release build of the same layout) to:

```
C:\PIXERA\data\rxExt\ExtGenesis\
```

Or browse to the folder via PIXERA's generic RenderBridge extension picker.

Use the **Release** `ExtGenesis.dll` (~3.5 MB) for production checks. Debug builds are larger and slower.

## Prerequisites (playback machine)

| Requirement | How to verify |
|-------------|---------------|
| Windows 10/11 x64 | `winver` |
| GPU driver with Vulkan 1.1+ | Device Manager → Display adapter; vendor driver installed |
| PIXERA with RenderBridge API 1.2 | Extension loads without API version mismatch |
| [VC++ 2015–2022 Redistributable (x64)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) | Install if DLL fails to load with missing `VCRUNTIME140.dll` / `MSVCP140.dll` |

**Not required:** Genesis Editor, CMake, Vulkan SDK, MSVC build tools.

## Folder layout

```
ExtGenesis/
  ExtGenesis.dll
  Genesis.manifest.json
  Scenes/
    demo.gns
    demo.scene.json
    showcase.gns
  Assets/
    test/          (bundled glTF meshes)
```

## Functional checks

- [ ] PIXERA loads `ExtGenesis.dll` without API version error
- [ ] Layer shows **Main** stream at expected resolution
- [ ] **Show Select** lists bundled scenes (`demo`, `Scenes/*.gns`)
- [ ] Selecting `Scenes/demo.gns` renders sphere + cube
- [ ] Switching to `Scenes/showcase.gns` reloads scene (previous scene kept on failure)
- [ ] **Position** / per-actor parameters move meshes in preview and output
- [ ] **Reload Scene** picks up editor-saved changes without restarting PIXERA
- [ ] Adapter LUID set correctly — output is not black
- [ ] Adding a new `.gns` under `Scenes/` appears in **Show Select** after a frame (may require stream restart for new actor parameters)

## Local RX.exe smoke test (optional)

On a dev machine with the RenderBridge SDK:

```powershell
third_party\render-bridge-sdk\RenderBridge\RX\RX.exe -script=extensions\ExtGenesis\run.lua
```

Confirm log lines: `GENESIS loaded show`, `GENESIS target_node`, no load errors.

## Failure triage

| Symptom | Likely cause |
|---------|----------------|
| DLL won't load | Missing VC++ redistributable or wrong architecture (need x64) |
| API version mismatch | Rebuild ExtGenesis from repo's vendored RenderBridge SDK 1.2 |
| Black output | Wrong adapter LUID |
| Scene not found | `Scenes/` folder missing next to DLL; check relative path in Show Select |
| Parameters stale after new scene file | Restart stream — RenderBridge registers parameters at stream creation; new actors are added incrementally when possible |

---

*Update this checklist when deployment layout or parameter schema changes.*
