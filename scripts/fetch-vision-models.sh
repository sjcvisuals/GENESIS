#!/usr/bin/env bash
# Print / prepare assets/vision for ExtGenesisRemover multi-model presets.
# Weights are not vendored (size + license); drop ONNX files into assets/vision/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT/assets/vision"
mkdir -p "$OUT_DIR"

cat <<EOF
Vision models directory: $OUT_DIR

Expected filenames (Vision Effect presets):
  depth_anything_v2_vits.onnx   — Depth Map
  birefnet_512_fp16.onnx        — BiRefNet Matte
  u2net_saliency.onnx           — Saliency
  human_parse.onnx              — Human Parse
  style_mosaic.onnx             — Colour / Style

See assets/vision/README.md for shapes / activation.
Person Matte (RVM): bash scripts/fetch-rvm-model.sh
EOF

ls -la "$OUT_DIR" || true
