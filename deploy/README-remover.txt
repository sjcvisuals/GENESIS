Genesis Remover — PIXERA background cutout (free, matte only)
=============================================================

This module is ONLY live-video background removal. It has no scene renderer,
lights, cameras, or actors. Use full ExtGenesis when you need GENESIS scenes.

Quick start (PIXERA)
--------------------
1. Install with GenesisRemoverSetup-*.exe  — or unzip GenesisRemover-*.zip
2. In PIXERA: Compositing → generic RenderBridge extension
3. Browse to ExtGenesisRemover.dll (keep this whole folder together)
4. Drag SDI / NDI / media → Video In
5. Output sampler = transparent person cutout (applies_alpha)

PIXERA handles the live SDI (or NDI / file) ingest. Genesis Remover only
cuts the person out in realtime (RVM ONNX, async — no NVIDIA Broadcast).

Install locations
-----------------
Wizard "Install into PIXERA":
  <PIXERA>/data/rxExt/ExtGenesisRemover/

Portable / ZIP / generic-extension browse:
  Any folder you choose — point PIXERA at ExtGenesisRemover.dll inside it.

Required files (must stay together)
-----------------------------------
  ExtGenesisRemover.dll
  GenesisRemover.manifest.json
  onnxruntime.dll
  assets/matting/rvm_mobilenetv3_fp16.onnx
  HOW-TO-PIXERA.txt   (this guide, short form)

Runtime dependency
------------------
Built with MSVC — needs Visual C++ 2015–2022 Redistributable (x64).
The Setup wizard installs it when missing.
Vulkan-capable GPU driver. No Genesis Editor, Vulkan SDK, or CMake.

Build / package (developers)
----------------------------
  .\scripts\build.ps1 -RemoverOnly
  .\scripts\package-remover-installer.ps1

Outputs:
  build\package\GenesisRemoverSetup-0.1.0.exe   ← free download installer
  build\package\GenesisRemover-0.1.0.zip        ← portable free download

See docs/genesis-remover-install.md and docs/video-bg-remover.md.
