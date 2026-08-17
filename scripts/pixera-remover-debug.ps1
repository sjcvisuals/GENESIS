# Quick checklist after installing Remover v14 (shared Vulkan).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v14 — PIXERA debug checklist
============================================
v13 and earlier used host download_texture for Video In. That API never
completes in PIXERA (stuck blue). v14 uses the SAME shared-Vulkan path as
the working ExtGenesis 'Background Remover' mode.

1) Quit PIXERA completely
2) git pull  (branch cursor/remover-camera-debug-7628)
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat   (confirm stamp remover-timeline-v14-shared-*)
5) Re-import ExtGenesisRemover.dll from data\rxExt\ExtGenesisRemover\
6) Add Remover to timeline, scrub until you see CYAN
7) Link webcam / media to parameter 'Video In'
8) Watch colour + Debug Stage / Debug Status on the resource:

   cyan     waiting_video_in   — link Video In (or wait ~1s for auto allocate)
   blue     download_fail      — Vulkan shared read failed (dump log)
   orange   input_black        — PIXERA not writing into shared texture yet
   live     passthrough/matte  — working

9) Dump log:
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dump-remover-log.ps1

Paste the 'latest STATUS lines' + stamp if it still fails.

"@

& "$PSScriptRoot\dump-remover-log.ps1"
