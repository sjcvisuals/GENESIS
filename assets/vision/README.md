# Vision ONNX models (ExtGenesisRemover / BgRemover)

Person cutout defaults to **RVM** (`assets/matting/rvm_mobilenetv3_fp16.onnx`).
Other **Vision Effect** presets load models from this directory.

| Effect | Default file | Expected output | Activation |
|--------|--------------|-----------------|------------|
| Depth Map | `depth_anything_v2_vits.onnx` | `[1,1,H,W]` map | MinMax |
| BiRefNet Matte | `birefnet_512_fp16.onnx` | `[1,1,512,512]` logits | Sigmoid |
| Saliency | `u2net_saliency.onnx` | single-channel map | MinMax |
| Human Parse | `human_parse.onnx` | single-channel / class map | MinMax |
| Colour / Style | `style_mosaic.onnx` | `[1,3,H,W]` RGB | n/a |
| Custom ONNX | *(Model File path)* | map or colour auto | Auto |

Supported graph shapes (same as ExtOnnxVision):

- `[1,1,H,W]` or `[1,H,W]` → single-channel map (Result As: Alpha / Greyscale / Both)
- `[1,3,H,W]` → colour image (opaque RGBA)
- Anything else → logged and frames pass through (Optical Flow / Face / Detect need decoders)

Place `.onnx` files here (or set **Model File**), then rebuild/install the Remover so
`assets/vision/` is copied next to `GenesisRemoverMatteHost.exe`.

```bash
bash scripts/fetch-rvm-model.sh          # person matte (required for RVM)
bash scripts/fetch-vision-models.sh      # prints expected filenames
```
