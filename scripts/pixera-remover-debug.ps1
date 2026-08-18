# Quick checklist after installing Remover v28 (Video In is_target=true).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v28 - vin-target
================================
v27: entire frame opaque black (max_rgb=0, A=1) — PIXERA clears, never blits feed.

v28: publish Video In as is_target=1 (SRV|RTV) so PIXERA can composite into it.

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v28-vin-target-*)
5) Re-import, Adapter LUID, Video In PLAYING
6) Dump log — want nz>0 or frame stats max_rgb>0

If still max_rgb=0: load ExtSampleD3D with the SAME Video In link.
  Sample black too → PIXERA source/routing
  Sample shows video → report back

"@

& "$PSScriptRoot\dump-remover-log.ps1"
