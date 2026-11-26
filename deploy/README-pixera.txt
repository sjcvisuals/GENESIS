Genesis PIXERA RenderBridge Extension
=====================================

Local WIP testing
-----------------
After double-clicking BUILD-AND-RUN.bat, use:

  <repo>\out\ExtGenesis\ExtGenesis.dll

Point PIXERA at that DLL. Keep the sibling folders next to it
(assets\, scenes\, shaders\, onnxruntime.dll).

Install (playback machine)
--------------------------
Copy the entire ExtGenesis folder to:
  <PIXERA>/data/rxExt/ExtGenesis/

Required files
--------------
  ExtGenesis.dll
  Genesis.manifest.json
  scenes/   (drop your .genesis / .gns shows here — fills the Scene dropdown)
  assets/   (bundled resources; assets/scenes/ is also scanned into Show)

Runtime dependency
------------------
ExtGenesis is built with MSVC and requires the Visual C++ 2015–2022 Redistributable (x64):
  https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

The Genesis Editor is NOT required on playback machines.

Scene loading
-------------
- Select shows via the RenderBridge parameter "Show" (show_select).
- The dropdown is filled from folders next to ExtGenesis.dll:
    scenes/
    assets/scenes/
- Paths are resolved relative to the extension DLL directory.

See docs/pixera-integration.md and docs/pixera-clean-machine-checklist.md.
