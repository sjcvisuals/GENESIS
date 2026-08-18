# Print Remover diagnostics so we can see what PIXERA actually loaded.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dump-remover-log.ps1

$ErrorActionPreference = "Continue"
$log = Join-Path $env:TEMP "ExtGenesisRemover.log"
Write-Host "Log: $log"
if (-not (Test-Path $log)) {
    Write-Host "MISSING - PIXERA has not loaded ExtGenesisRemover.dll yet (or wrong DLL path)." -ForegroundColor Red
    Write-Host "After install, fully quit PIXERA, re-import the Remover DLL, scrub onto the clip."
    exit 1
}

Write-Host ""
Write-Host "==== stamp (must be v44-oop-matte) ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "remover-timeline-" | ForEach-Object { $_.Line } |
    ForEach-Object { if ($_ -match '\[(remover-timeline-[^\]]+)\]') { $Matches[1] } } |
    Group-Object | Sort-Object Count -Descending |
    ForEach-Object { "{0,5}  {1}" -f $_.Count, $_.Name }

Write-Host ""
Write-Host "==== latest STATUS lines (stage machine) ====" -ForegroundColor Cyan
Write-Host "  cyan/waiting_video_in = empty Video In slot (link Live Input)"
Write-Host "  passthrough           = live blit (waiting on out-of-process matte)"
Write-Host "  matte                 = cutout from GenesisRemoverMatteHost.exe"
Select-String -Path $log -Pattern "STATUS stage=" | Select-Object -Last 15 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== key events ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "MatteHost|gpu_present|applied host|kick|host_ready|STATUS stage=|shared VIDEO_IN|CAPTURE|oop" |
    Select-Object -Last 50 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== last 40 lines ====" -ForegroundColor Cyan
Get-Content $log -Tail 40

$hostLog = Join-Path $env:TEMP "GenesisRemoverMatteHost.log"
Write-Host ""
Write-Host "==== matte host log (tail) ====" -ForegroundColor Cyan
Write-Host "Log: $hostLog"
if (Test-Path $hostLog) {
    Get-Content $hostLog -Tail 40
} else {
    Write-Host "MISSING — host not started. Ensure GenesisRemoverMatteHost.exe is next to the DLL."
}

Write-Host ""
Write-Host "Expect LIVE lime, then host_ready=1, matte_ok in host log, applied host matte."
Write-Host "Adapter LUID = PREVIEW engine GPU when testing preview."
