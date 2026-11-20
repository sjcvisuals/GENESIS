# Live video + background remover

## Goals

- Ingest live video (Pixera sampler / later NDI·SDI) into GENESIS
- Cut out people in realtime **without** the NVIDIA Broadcast / Maxine SDK
- Keep a shared matte core for ExtGenesis **Background Remover** mode

The free matte-only PIXERA product (`ExtGenesisRemover`) lives in
[`sjcvisuals/GenesisRemover`](https://github.com/sjcvisuals/GenesisRemover).

## Architecture

| Piece | Role |
|-------|------|
| `ExternalSharedImage` | Pixera→DLL shared RGBA |
| `BgRemover` + `RvmOnnxSession` | Auto → **RVM ONNX** (preferred) or chroma key |
| `AsyncMatteWorker` | One-inflight worker; skip when busy; reuse last matte |
| `RemoverPipeline` | video_in → async matte → exportable RGBA |
| ExtGenesis `mode` | Scene **or** Background Remover (+ Quality) |

### Pixera — full GENESIS (`ExtGenesis`)

1. Mode = **Background Remover**
2. Link media/NDI/SDI → **Video In**, pick **Quality**
3. Output sampler = transparent cutout

### Pixera — Genesis Remover (free cutout module)

Use [`sjcvisuals/GenesisRemover`](https://github.com/sjcvisuals/GenesisRemover)
(`ExtGenesisRemover.dll`). Install / packaging docs live there.

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
