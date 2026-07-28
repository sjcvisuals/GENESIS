# Cursor Cloud

## Install / update script

Cloud agents run `scripts/cloud-install.sh` (via `.cursor/environment.json`).

That script:
1. Deletes stale `build/_deps/*-subbuild` directories (prevents Ninja “build.ninja still dirty” during Vulkan-Headers FetchContent).
2. Reconfigures with `cmake -B build -G Ninja`.

On Linux VMs with `libvulkan-dev` installed, `cmake/VulkanSetup.cmake` prefers `/usr/include/vulkan` and skips FetchContent entirely.

## Build / run notes

- Configure: `bash scripts/cloud-install.sh` or `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build -j$(nproc) --target genesis_gltf_viewer`
- Audio smoke: `./build/genesis_audio_smoke`
- Matting smoke: `./build/genesis_matting_smoke` (chroma + RVM + async skip/reuse)
- Optional RVM weights: `bash scripts/fetch-rvm-model.sh` (or commit `assets/matting/*.onnx`)
- ONNX Runtime: CMake option `GENESIS_ENABLE_ONNXRUNTIME` (default ON) downloads into `third_party/onnxruntime-*` (gitignored)
- Editor needs a display (`DISPLAY=:1` with XFCE/lavapipe in the cloud snapshot).

See `docs/video-bg-remover.md` for live video / background remover + Genesis Remover status.
