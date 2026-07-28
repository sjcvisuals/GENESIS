# Live video + background remover

## Goals

- Ingest live video (Pixera sampler / later NDI·SDI) into GENESIS
- Cut out people in realtime **without** requiring the NVIDIA Broadcast / Maxine SDK
- Share one matte core with a future slim **Genesis Remover** Pixera fork

## Architecture (Step 1 — landed)

| Piece | Role |
|-------|------|
| `ExternalSharedImage` | Pixera→DLL shared RGBA (same pattern as ExtSampleD3D) |
| `BgRemover` | CPU matte: Auto → RVM(ONNX) if available, else chroma key |
| `RemoverPipeline` | video_in download → matte → upload exportable output |
| ExtGenesis `mode` | `Scene` (existing 3D) or `Background Remover` |
| ExtGenesis `texture` | Video In parameter for Pixera to feed |

### Pixera Remover workflow

1. Create GENESIS stream, set **Mode = Background Remover**
2. Link a Pixera media/NDI/SDI resource into **Video In** (`texture`)
3. GENESIS writes a transparent RGBA cutout to the output **sampler**
4. Stack Pixera effects on that sampler

### Efficiency notes

- Preferred AI path: **RVM MobileNetV3 FP16** via ONNX Runtime (CUDA/DirectML when present) — soft alpha + temporal state, no Broadcast SDK
- Fast path: chroma key (keyed stages) — microseconds/frame class on CPU
- Current matte stage is CPU RGBA for correctness; GPU ORT EP + zero-copy is the next perf pass
- Infer at reduced resolution when RVM is enabled (`rvm_downsample`, quality presets)

### Step 2 — Genesis Remover fork (planned)

Strip ExtGenesis to: Video In → matte → sampler out. Same `genesis_matting` + `RemoverPipeline`. No scene graph / fluids / OpenPBR. Ideal free Pixera “human cutout” module.

### Assets

```bash
bash scripts/fetch-rvm-model.sh   # → assets/matting/rvm_mobilenetv3_fp16.onnx
```

Build flag for ORT linking (future): `GENESIS_HAS_ONNXRUNTIME`.

### Smoke test

```bash
./build/genesis_matting_smoke
```
