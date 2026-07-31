Genesis Remover — PIXERA background cutout (matte only)
=======================================================

This module is ONLY live-video background removal. No scenes, lights, or actors.
Use ExtGenesis when you need GENESIS scenes.

IMPORTANT — two different modules
---------------------------------
  ExtGenesis          = full GENESIS scene bridge
  ExtGenesisRemover   = background cutout only

They are NOT interchangeable. Do not drop ExtGenesisRemover.dll into
data\rxExt\ExtGenesis\. Use a separate ExtGenesisRemover folder.

Local WIP testing
-----------------
After double-clicking BUILD-AND-RUN.bat, use:

  <repo>\out\ExtGenesisRemover\ExtGenesisRemover.dll

Point PIXERA at that DLL. Keep sibling onnxruntime.dll + assets\matting\.

Or copy the whole folder into PIXERA:

  <PIXERA>\data\rxExt\ExtGenesisRemover\

Then browse to ExtGenesisRemover.dll inside THAT folder.

Verify before PIXERA
--------------------
  .\scripts\check-pixera-dll.ps1

Required files
--------------
  ExtGenesisRemover.dll
  GenesisRemover.manifest.json
  assets/matting/rvm_mobilenetv3_fp16.onnx
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
Run .\scripts\check-pixera-dll.ps1 and fix anything marked [MISS]/[FAIL].
Typical causes:
  - onnxruntime.dll missing next to the DLL
  - browsed a Debug build (BUILD-AND-RUN now ships Release for Pixera DLLs)
  - copied Remover into data\rxExt\ExtGenesis\ by mistake
  - VC++ 2015-2022 x64 redistributable missing

See docs/video-bg-remover.md.
