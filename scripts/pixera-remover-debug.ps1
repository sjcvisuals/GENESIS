# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v56 - ExtOnnxVision lessons
===========================================
Adapted from blackdown/pixera-onnx-plugin (working in PIXERA 26.1):

  - NO Device TimelineSemaphore / GPU Wait in render
    (our fence Wait turned shared-Map stalls into sticky 2-3fps)
  - Matte kick from update(), skip when busy (never queue)
  - Present path only: live Video In x last matte alpha
  - map_ms cooldown still present as safety net

Expect:
  stamp remover-timeline-v56-extonnxvision-nofence-*
  log: "(no device fence)" / matte_kick from update path

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Preview OK -> enable outputs -> disable outputs (should recover)
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
