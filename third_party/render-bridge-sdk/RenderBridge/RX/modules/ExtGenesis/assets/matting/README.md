# Matting models

GENESIS background removal looks for:

`assets/matting/rvm_mobilenetv3_fp16.onnx`

Download with:

```bash
bash scripts/fetch-rvm-model.sh
```

That pulls the official **Robust Video Matting** MobileNetV3 FP16 ONNX (~7 MB) from the project release assets. No NVIDIA Broadcast / Maxine SDK install is required.

With `GENESIS_ENABLE_ONNXRUNTIME=ON` (default), CMake downloads ONNX Runtime into `third_party/` and Auto mode prefers RVM. Without the model file or ORT, Auto falls back to realtime **chroma key** (still useful for keyed Pixera sources).
