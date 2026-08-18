# Live video + background remover

## Goals

- Ingest live video (Pixera sampler / later NDI·SDI) into GENESIS
- Cut out people in realtime **without** the NVIDIA Broadcast / Maxine SDK
- Share one matte core with the slim **Genesis Remover** Pixera module

## Architecture

| Piece | Role |
|-------|------|
| `ExternalSharedImage` | Pixera→DLL shared RGBA |
| `BgRemover` + `RvmOnnxSession` | Auto → **RVM ONNX** (preferred) or chroma key |
| `AsyncMatteWorker` | One-inflight worker; skip when busy; reuse last matte |
| `RemoverPipeline` | video_in → async matte → exportable RGBA |
| ExtGenesis `mode` | Scene **or** Background Remover (+ Quality) |
| **ExtGenesisRemover** | Matte-only Pixera DLL |

### Pixera — full GENESIS

1. Mode = **Background Remover**
2. Link media/NDI/SDI → **Video In**, pick **Quality**
3. Output sampler = transparent cutout

### Pixera — Genesis Remover (free cutout module)

1. Load `ExtGenesisRemover` instead of full GENESIS
2. Link video → **Video In**, pick Quality
3. Output is only the cutout — stack Pixera effects on top

## Efficiency

- **RVM MobileNetV3 FP16** via ONNX Runtime (**DirectML/GPU on Windows**, CPU fallback; CUDA EP later)
- Infer at reduced res (`Fast`/`Balanced`/`Quality` → downsample ~0.20–0.35, bilinear upsample)
- **Async by default**: Pixera `render()` never waits on `Ort::Run`. Frames submitted while the worker is busy replace a latest-wins pending slot; the last completed matte is reused for display.
- Chroma key remains the zero-dep fallback for keyed stages
- No NVIDIA Broadcast SDK install

## Assets / build

```bash
bash scripts/fetch-rvm-model.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target genesis_matting_smoke
./build/genesis_matting_smoke
```

CMake option: `GENESIS_ENABLE_ONNXRUNTIME` (default ON) downloads ORT into `third_party/`.

## Smoke test

```bash
./build/genesis_matting_smoke
```

Covers chroma, sync RVM, and async skip/reuse stats.

## Isolate matte from PIXERA (webcam debug)

When Remover misbehaves in PIXERA, **do not keep guessing I/O vs RVM**. Run the
standalone camera tool that uses the same `AsyncMatteWorker` + RVM path with the
laptop webcam (SDL3), no RenderBridge:

```powershell
.\scripts\build.ps1 -CameraDebugOnly
.\scripts\run-remover-camera-debug.ps1
```

Or: `.\scripts\diagnose-remover.ps1` (prints the decision tree + dumps logs).

| Result | Meaning |
|--------|---------|
| Right pane: live person cutout over checkerboard, log `rvm=1` | Matte works. Bug is PIXERA Video In / host download-upload. |
| Log `rvm=0` / only chroma (green key) | ORT or `assets/matting/*.onnx` broken — fix shared deps before PIXERA. |
| No cameras / no frames | OS camera permission, device busy, or try `--camera N`. |

Log file: `out\RemoverCameraDebug\remover-camera-debug.log`

Keys: `1`/`2`/`3` quality, `P` opaque passthrough, `Esc` quit.

On-screen overlay reports **cam FPS**, **matte FPS** (RVM completes/sec), **infer ms**,
and **display FPS**. Right pane reuses the last alpha on every camera frame so motion
stays smooth even when RVM is slower than the camera.

Press `1` (Fast) for max matte FPS; `3` (Quality) for sharper edges / lower FPS.

### PIXERA log after camera debug proves matte

```powershell
.\scripts\dump-remover-log.ps1
```

Look for `STATUS stage=` lines and stamp `remover-timeline-v31-shared-fp16-*`.

| Colour / stage | Meaning |
|----------------|---------|
| cyan / `waiting_video_in` | shared output OK, Video In not published |
| blue / `download_fail` | shared Vulkan read failed |
| orange / `input_black` | Video In bound, PIXERA wrote zeros |
| `passthrough` / `matte` | working |

v31: host `download_texture` on Video In hangs (do not use). Shared-Vulkan path
matches PIXERA FP16 via `ensureVideoIn(VkFormat)` like ExtGenesis Background Remover.

Also check Remover params **Debug Stage** / **Debug Status**.
Checklist: `.\scripts\pixera-remover-debug.ps1`
