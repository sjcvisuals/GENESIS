# Vision ONNX models (ExtGenesisRemover / BgRemover)

Person cutout defaults to **RVM** (`assets/matting/rvm_mobilenetv3_fp16.onnx`).
Other **Vision Effect** presets load models from this directory (vendored below).

| Effect | File | Source |
|--------|------|--------|
| Depth Map | `depth_anything_v2_vits.onnx` (~95 MB) | [fabio-sim/Depth-Anything-ONNX v2.0.0](https://github.com/fabio-sim/Depth-Anything-ONNX/releases/tag/v2.0.0) |
| BiRefNet Matte | `birefnet_512_fp16.onnx` (~452 MB) | [onnx-community/BiRefNet_512x512-ONNX](https://huggingface.co/onnx-community/BiRefNet_512x512-ONNX) `onnx/model_fp16.onnx` |
| Saliency | `u2net_saliency.onnx` (~168 MB) | [rembg u2net.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx) |
| Human Parse | `human_parse.onnx` (~168 MB) | [rembg u2net_human_seg.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net_human_seg.onnx) (person seg map) |
| Colour / Style | `style_mosaic.onnx` (~6.5 MB) | [onnx/models mosaic-9](https://github.com/onnx/models/raw/main/validated/vision/style_transfer/fast_neural_style/model/mosaic-9.onnx) |

Re-fetch / refresh:

```bash
bash scripts/fetch-vision-models.sh
```

Large `.onnx` files are stored with **Git LFS**. After clone: `git lfs pull`.

Supported graph shapes (same as ExtOnnxVision):

- `[1,1,H,W]` or `[1,H,W]` → single-channel map (Result As: Alpha / Greyscale / Both)
- `[1,3,H,W]` → colour image (opaque RGBA)
