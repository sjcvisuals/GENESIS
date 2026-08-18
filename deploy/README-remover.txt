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

Recommended (same pattern as your working ExtGenesis install):

  .\scripts\install-pixera-remover.ps1

That copies into:

  <PIXERA>\data\rxExt\ExtGenesisRemover\

Then browse PIXERA to ExtGenesisRemover.dll inside THAT folder (not ExtGenesis).

Verify before PIXERA
--------------------
  .\scripts\check-pixera-dll.ps1

Required files
--------------
  ExtGenesisRemover.dll
  GenesisRemover.manifest.json
  assets/matting/rvm_mobilenetv3_fp16.onnx
  onnxruntime.dll
  DirectML.dll          (Windows GPU path; CPU still works if missing)

Usage in PIXERA
---------------
1. Load ExtGenesisRemover as a generic RenderBridge extension resource.
2. Drag it onto the timeline (stream name is Main, same as ExtGenesis).
3. Link media / NDI / SDI -> Video In.
4. Set Quality (Fast / Balanced / Quality).
5. Output sampler is a transparent RGBA cutout.

If the resource imports but timeline drag fails
-----------------------------------------------
1. Fully quit PIXERA (so the old DLL unlocks), then:

     git pull
     .\BUILD-AND-RUN.bat
     powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-pixera-remover.ps1

2. Confirm the install printed BUILD_STAMP with:
     remover-timeline-v20-d3d-io-20260817

3. In PIXERA, re-browse to:
     <PIXERA>\data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll
   (not ExtGenesis, not Documents\...\out\ unless you intend to)

4. Open the always-on log (created even if timeline init fails):

     %TEMP%\ExtGenesisRemover.log

   If that file is missing after loading the resource, PIXERA is not loading
   this DLL at all (wrong path / locked old copy / need restart).

5. Optional second log next to the DLL:
     <PIXERA>\data\rxExt\ExtGenesisRemover\remover-init.log

Build stamp in logs / BUILD_STAMP.txt:
  remover-timeline-v42-preload-cpu-20260818

v42 = preload CPU RVM + release share before infer (no Vulkan in PIXERA DLL path):
  v41 got nz>0 then froze during BgRemover/ORT init while the capture share was still
  open; Input also constructed RemoverPipeline/Vulkan on the preview GPU.
  Fix: link genesis_matting only (no remover_core/Vulkan); preload ORT on worker
  start before kicks; release OpenSharedResource before process().

v41 = gentle capture but ORT init while share held still stalled PIXERA.
v40 = OpenSharedResource storm + Flush.
v36 = blit-only diagnostic (live feed proven).

Dump: .\scripts\dump-remover-log.ps1
Expect stamp v42-preload-cpu, preload OK before kick, ep=CPU, matte_ok,
continuously moving lime.

Open diagnostics:
  %TEMP%\ExtGenesisRemover.log

Prove matte outside PIXERA first
--------------------------------
If the timeline is still black/cyan/frozen, isolate RVM from PIXERA I/O:

  .\scripts\run-remover-camera-debug.ps1

That opens the laptop camera (left) and the cutout (right). Decision tree:

  .\scripts\diagnose-remover.ps1

Build only the debug exe:

  .\scripts\build.ps1 -CameraDebugOnly

If PIXERA says "Can Not Load Pixera Extension"
---------------------------------------------
Run .\scripts\check-pixera-dll.ps1 and fix anything marked [MISS]/[FAIL].
Typical causes:
  - onnxruntime.dll missing next to the DLL
  - browsed a Debug build (BUILD-AND-RUN now ships Release for Pixera DLLs)
  - copied Remover into data\rxExt\ExtGenesis\ by mistake
  - VC++ 2015-2022 x64 redistributable missing
  - PIXERA still has the old DLL locked — fully quit and reinstall

See docs/video-bg-remover.md.
