# Quick checklist after installing Remover v27 (delay + frame stats).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v27 - delay-stats
=================================
v26: download OK, PIXERA writes opaque black (mid A=255, RGB=0).

v27:
  - Process previous frame (1-frame delay) in case fence is early
  - Log frame stats: max_rgb, corners (letterbox check)
  - Video In = ExtSampleD3D SRV-only again

If still orange with max_rgb=0:
  A/B test ExtSampleD3D with the SAME Video In media link.
  If Sample is also black → PIXERA routing/source, not Remover.
  If Sample shows video → Remover read path still wrong.

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v27-delay-stats-*)
5) Re-import, Adapter LUID, Video In PLAYING
6) Dump log — send STATUS + any 'frame stats' / 'opaque_black' lines

"@

& "$PSScriptRoot\dump-remover-log.ps1"
