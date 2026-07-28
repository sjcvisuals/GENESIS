# Matting models

GENESIS background removal looks for:

`assets/matting/rvm_mobilenetv3_fp16.onnx`

Download with:

```bash
bash scripts/fetch-rvm-model.sh
```

That pulls the official **Robust Video Matting** MobileNetV3 FP16 ONNX (~7 MB) from the project release assets. No NVIDIA Broadcast / Maxine SDK install is required.

Until ONNX Runtime is linked (`GENESIS_HAS_ONNXRUNTIME`), Auto mode falls back to realtime **chroma key** (still useful for keyed Pixera sources).
