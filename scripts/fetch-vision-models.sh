#!/usr/bin/env bash
# Download vision ONNX weights into assets/vision/ (idempotent).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT/assets/vision"
mkdir -p "$OUT_DIR"

fetch_one() {
  local url="$1"
  local out="$2"
  if [[ -f "$out" ]] && [[ $(stat -c%s "$out" 2>/dev/null || stat -f%z "$out") -gt 1000000 ]]; then
    echo "Already present: $out ($(du -h "$out" | cut -f1))"
    return 0
  fi
  echo "Downloading $url"
  echo "  -> $out"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --retry 3 -o "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$out" "$url"
  else
    echo "Need curl or wget" >&2
    exit 1
  fi
  echo "Wrote $out ($(du -h "$out" | cut -f1))"
}

fetch_one \
  "https://github.com/fabio-sim/Depth-Anything-ONNX/releases/download/v2.0.0/depth_anything_v2_vits.onnx" \
  "$OUT_DIR/depth_anything_v2_vits.onnx"

fetch_one \
  "https://huggingface.co/onnx-community/BiRefNet_512x512-ONNX/resolve/main/onnx/model_fp16.onnx" \
  "$OUT_DIR/birefnet_512_fp16.onnx"

fetch_one \
  "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx" \
  "$OUT_DIR/u2net_saliency.onnx"

fetch_one \
  "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net_human_seg.onnx" \
  "$OUT_DIR/human_parse.onnx"

fetch_one \
  "https://github.com/onnx/models/raw/main/validated/vision/style_transfer/fast_neural_style/model/mosaic-9.onnx" \
  "$OUT_DIR/style_mosaic.onnx"

echo
echo "Vision models in $OUT_DIR:"
ls -lh "$OUT_DIR"/*.onnx 2>/dev/null || true
