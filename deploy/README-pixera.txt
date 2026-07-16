Genesis PIXERA RenderBridge Extension
=====================================

Install
-------
Copy the entire GenesisPixera folder to your PIXERA RenderBridge extensions directory.
Typical path:
  <PIXERA>/data/rxExt/ExtGenesis/

Required files
--------------
  ExtGenesis.dll          (~3.5 MB Release; use Release builds for shows)
  Genesis.manifest.json
  Scenes/   (show packages and scene documents)
  Assets/   (meshes, textures, bundled resources)

Runtime dependency
------------------
ExtGenesis is built with MSVC and requires the Visual C++ 2015–2022 Redistributable (x64)
on playback machines:
  https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

The Genesis Editor is NOT required on playback machines.

Topology examples (Assets/topology/) are included for reference. Multi-output
simulation tools live with Genesis Player, not inside this PIXERA folder.

Scene loading
-------------
- Select shows via the RenderBridge parameter "Show Select" (show_select).
- Paths are resolved relative to the extension DLL directory, not the process working directory.
- Built-in demo is available when no scene files are present.

Reload
------
Change "Show Select" to load another scene. Toggle "Reload Scene" after saving in the editor.
If loading fails, the previous scene stays active. New `.gns` files under Scenes/ appear in
Show Select automatically; restart the stream if new actors need fresh parameters.

See docs/pixera-clean-machine-checklist.md for full verification steps.

Local test (developer machine)
------------------------------
Build ExtGenesis, then run RX.exe with extensions/ExtGenesis/run.lua from the RenderBridge SDK.

Clean-machine requirements
--------------------------
- Windows 10/11 x64
- Vulkan 1.1+ compatible GPU driver (installed with GPU vendor driver)
- PIXERA with RenderBridge API 1.2

NOT required on playback machines
---------------------------------
- Genesis Editor
- Visual Studio / MSVC build tools
- Vulkan SDK
- CMake
- Source code
