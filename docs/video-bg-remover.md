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

- **RVM MobileNetV3 FP16** via ONNX Runtime (CPU today; CUDA/DirectML EP later)
- Infer at reduced res (`Fast`/`Balanced`/`Quality` → downsample ~0.2–0.35)
- **Async by default**: Pixera `render()` never waits on `Ort::Run`. Frames submitted while the worker is busy are skipped; the last completed matte is uploaded (1+ frame latency tradeoff).
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
