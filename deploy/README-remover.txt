Genesis Remover — PIXERA background cutout (matte only)
=======================================================

This module is ONLY live-video background removal. It has no scene renderer,
lights, cameras, or actors. Use full ExtGenesis when you need GENESIS scenes.

Install
-------
Copy the entire GenesisRemover folder to your PIXERA RenderBridge extensions
directory. Typical path:
  <PIXERA>/data/rxExt/ExtGenesisRemover/

Required files
--------------
  ExtGenesisRemover.dll
  GenesisRemover.manifest.json
  assets/matting/          (RVM ONNX model)
  onnxruntime.dll          (copied next to the DLL when ORT is enabled)

Usage in PIXERA
---------------
1. Load ExtGenesisRemover as a generic RenderBridge extension.
2. Create the Cutout stream.
3. Link media / NDI / SDI → Video In.
4. Set Quality (Fast / Balanced / Quality).
5. Output sampler is a transparent RGBA cutout (applies_alpha).

Runtime dependency
------------------
Built with MSVC — needs Visual C++ 2015–2022 Redistributable (x64).
Vulkan-capable GPU driver. No Genesis Editor, Vulkan SDK, or CMake on playback.

See docs/video-bg-remover.md for architecture (RVM + async matte worker).
