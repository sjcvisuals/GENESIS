# Quick checklist after installing Remover.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\pixera-remover-debug.ps1

$ErrorActionPreference = 'Continue'
Write-Host @'

GENESIS Remover v48 - DirectML GPU matte host
=============================================
RVM should run on GPU via DirectML in GenesisRemoverMatteHost.exe
(not inside PIXERA). Bind DML to the PIXERA Adapter LUID.

Expect:
  stamp remover-timeline-v48-oop-gpu-*
  host: BgRemover ready ep=DML:N  (NOT ep=CPU)
  DLL STATUS ep=DML:N
  DirectML.dll next to the host exe
  faster cutout (640-wide Fast capture + shorter settle)

If ep=CPU: rebuild/install so onnxruntime DirectML build + DirectML.dll
sit next to GenesisRemoverMatteHost.exe.

1) Quit PIXERA completely
2) git pull
3) BUILD-REMOVER.bat
4) INSTALL-PIXERA-REMOVER.bat
5) Re-import ExtGenesisRemover.dll
6) Adapter LUID = preview GPU; drag Live Input
7) Dump log

'@

& "$PSScriptRoot\dump-remover-log.ps1"
