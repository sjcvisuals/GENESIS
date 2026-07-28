#!/usr/bin/env bash
# Fetch Robust Video Matting ONNX weights (no NVIDIA Broadcast SDK).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT/assets/matting"
OUT="$OUT_DIR/rvm_mobilenetv3_fp16.onnx"
mkdir -p "$OUT_DIR"

if [[ -f "$OUT" ]]; then
  echo "Already present: $OUT"
  exit 0
fi

URL="https://github.com/PeterL1n/RobustVideoMatting/releases/download/v1.0.0/rvm_mobilenetv3_fp16.onnx"
echo "Downloading $URL"
if command -v curl >/dev/null 2>&1; then
  curl -L --fail -o "$OUT" "$URL"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$OUT" "$URL"
else
  echo "Need curl or wget" >&2
  exit 1
fi
echo "Wrote $OUT"
