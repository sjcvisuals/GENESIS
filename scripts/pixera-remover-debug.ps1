# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = "Continue"
Write-Host @"

GENESIS Remover v46 - OOP matte settle + ack
===========================================
v45 broke cutout: Sleep(8) -> capture nz=0, host never acked done_seq,
DLL stuck busy=1 forever (live camera + lime only).

v46: host Sleep(50) + nz=0 remap retry; ALWAYS set done_seq on failure;
DLL clears stale busy after 2s.

Expect:
  stamp remover-timeline-v46-oop-settle-*
  host matte_ok (not stuck on capture_nz0 without further kicks)
  DLL applied host matte / stage=matte

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat  (stamp remover-timeline-v46-oop-settle-*)
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

"@

& "$PSScriptRoot\dump-remover-log.ps1"
