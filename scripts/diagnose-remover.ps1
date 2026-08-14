# Decision-tree diagnostics for ExtGenesisRemover failures.
# Prints what to run next and dumps the PIXERA Remover log if present.

param(
    [switch]$SkipLog
)

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot

Write-Host ""
Write-Host "=== GENESIS Remover diagnose ===" -ForegroundColor Cyan
Write-Host @"

Step 1 - Prove the matte core outside PIXERA
  .\scripts\run-remover-camera-debug.ps1

  Look at the window:
    Left  = live camera (opaque)
    Right = cutout over checkerboard

  Also check out\RemoverCameraDebug\remover-camera-debug.log for:
    probe backend=... rvm=1   <- RVM/ORT healthy
    status ... completed=N    <- async worker producing mattes

Step 2 - Interpret
  A) Camera debug cutout WORKS, PIXERA still black/cyan/frozen
       -> Bug is ExtGenesisRemover I/O (Video In link / download_texture /
          upload_texture), not RVM. Dump the PIXERA log (below).

  B) Camera debug shows rvm=0 / chroma only / no person cutout
       -> Fix shared matte deps first:
            out\RemoverCameraDebug\onnxruntime.dll
            out\RemoverCameraDebug\assets\matting\rvm_mobilenetv3_fp16.onnx
          Same files must sit next to ExtGenesisRemover.dll after install.

  C) No cameras / permission denied / no frames after ~8s
       -> Windows camera privacy, another app locking the cam, or wrong --camera N.
          Try: .\scripts\run-remover-camera-debug.ps1 -List

Step 3 - PIXERA path (only after A)
  Quit PIXERA fully, then:
    .\BUILD-AND-RUN.bat   (or .\scripts\build.ps1 -RemoverOnly)
    .\scripts\install-pixera-remover.ps1
    .\scripts\check-pixera-dll.ps1
  Re-import ExtGenesisRemover, scrub until cyan, link webcam, wait ~2s.
  Then dump log:

"@

if (-not $SkipLog) {
    $dump = Join-Path $PSScriptRoot "dump-remover-log.ps1"
    if (Test-Path $dump) {
        Write-Host "----- %TEMP%\ExtGenesisRemover.log -----" -ForegroundColor Yellow
        & powershell -NoProfile -ExecutionPolicy Bypass -File $dump
    }
}

$camLog = Join-Path $RepoRoot "out\RemoverCameraDebug\remover-camera-debug.log"
if (Test-Path $camLog) {
    Write-Host ""
    Write-Host "----- last 40 lines: remover-camera-debug.log -----" -ForegroundColor Yellow
    Get-Content $camLog -Tail 40
}

Write-Host ""
Write-Host "Useful greps in ExtGenesisRemover.log:" -ForegroundColor Cyan
Write-Host "  download armed | download ok | video_in HOST | allocate | cyan | magenta | crash | ORT | RVM"
Write-Host ""
