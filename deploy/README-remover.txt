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

Wednesday industry demo
-----------------------
See deploy\DEMO-RUNBOOK-remover.md (stamp remover-demo-v61-live-fps-*).

Defaults: Person Matte (RVM), Quality Fast, Result As Alpha. Leave Model File empty.

Local WIP testing
-----------------
After double-clicking BUILD-AND-RUN.bat, use:

  <repo>\out\ExtGenesisRemover\ExtGenesisRemover.dll

Point PIXERA at that DLL. Keep sibling onnxruntime.dll + GenesisRemoverMatteHost.exe
+ assets\matting\ (+ assets\vision\ for multi-model).

Recommended (same pattern as your working ExtGenesis install):

  .\scripts\install-pixera-remover.ps1

That copies into:

  <PIXERA>\data\rxExt\ExtGenesisRemover\

Then browse PIXERA to ExtGenesisRemover.dll inside THAT folder (not ExtGenesis).

Verify before PIXERA
--------------------
  .\scripts\check-pixera-dll.ps1
  .\scripts\dump-remover-log.ps1

Required files
--------------
  ExtGenesisRemover.dll
  GenesisRemoverMatteHost.exe
  GenesisRemover.manifest.json
  BUILD_STAMP.txt
  assets/matting/rvm_mobilenetv3_fp16.onnx   (real blob, NOT a Git LFS pointer)
  assets/vision/*.onnx                       (optional for demo; LFS required for Depth/etc.)
  onnxruntime.dll
  DirectML.dll          (Windows GPU path; CPU still works if missing)

Usage in PIXERA
---------------
1. Load ExtGenesisRemover as a generic RenderBridge extension resource.
2. Drag it onto the timeline (stream name is Main, same as ExtGenesis).
3. Link media / NDI / SDI -> Video In.
4. Vision Effect = Person Matte (RVM), Quality = Fast, Result As = Alpha.
5. Confirm Debug Status shows remover-demo-v61-live-fps-* and matte=1.
6. Output sampler is a transparent RGBA cutout.

If the resource imports but timeline drag fails
-----------------------------------------------
1. Fully quit PIXERA (so the old DLL unlocks), then:

     git pull
     git lfs pull
     .\BUILD-REMOVER.bat
     powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-pixera-remover.ps1

2. Confirm the install printed BUILD_STAMP with:
     remover-demo-v61-live-fps-*

3. In PIXERA, re-browse to:
     <PIXERA>\data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll
   (not ExtGenesis, not Documents\...\out\ unless you intend to)

4. Open the always-on log (created even if timeline init fails):

     %TEMP%\ExtGenesisRemover.log

   If that file is missing after loading the resource, PIXERA is not loading
   this DLL at all (wrong path / locked old copy / need restart).

5. Optional second log next to the DLL:
     <PIXERA>\data\rxExt\ExtGenesisRemover\remover-init.log

Build stamp in logs / BUILD_STAMP.txt / Debug Status:
  remover-demo-v61-live-fps-20260913

Healthy host log (%TEMP%\GenesisRemoverMatteHost.log):
  start remover-demo-v61-live-fps-...
  RVM model OK bytes=...
  BgRemover ready ep=DML:N
  matte_ok ... map_ms=... infer_ms=...

If Debug Status shows MISMATCH: DLL and MatteHost disagree — reinstall the whole package.
