Genesis Remover — PIXERA background cutout (matte only)
=======================================================

This module is ONLY live-video background removal. No scenes, lights, or actors.
Use ExtGenesis when you need GENESIS scenes.

Local WIP testing
-----------------
After double-clicking BUILD-AND-RUN.bat, use:

  <repo>\out\ExtGenesisRemover\ExtGenesisRemover.dll

Point PIXERA at that DLL. Keep sibling assets\matting\ and onnxruntime.dll.

Install (playback machine)
--------------------------
Copy the entire ExtGenesisRemover folder to:
  <PIXERA>/data/rxExt/ExtGenesisRemover/

Required files
--------------
  ExtGenesisRemover.dll
  GenesisRemover.manifest.json
  assets/matting/          (RVM ONNX model)
  onnxruntime.dll

Usage in PIXERA
---------------
1. Load ExtGenesisRemover as a generic RenderBridge extension.
2. Create the Cutout stream.
3. Link media / NDI / SDI -> Video In.
4. Set Quality (Fast / Balanced / Quality).
5. Output sampler is a transparent RGBA cutout.

If PIXERA says "Can Not Load Pixera Extension"
---------------------------------------------
Almost always a LoadLibrary failure. Check:
  - You browsed to out\ExtGenesisRemover\ExtGenesisRemover.dll (Release build)
  - onnxruntime.dll sits in that same folder
  - assets\matting\rvm_mobilenetv3_fp16.onnx is present
  - Visual C++ 2015-2022 x64 redistributable is installed
Do NOT use a Debug copy of the DLL inside PIXERA.

See docs/video-bg-remover.md.
