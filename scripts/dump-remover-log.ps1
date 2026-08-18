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
Write-Host "==== stamp (must be v43-live-blit) ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "remover-timeline-" | ForEach-Object { $_.Line } |
    ForEach-Object { if ($_ -match '\[(remover-timeline-[^\]]+)\]') { $Matches[1] } } |
    Group-Object | Sort-Object Count -Descending |
    ForEach-Object { "{0,5}  {1}" -f $_.Count, $_.Name }

Write-Host ""
Write-Host "==== latest STATUS lines (stage machine) ====" -ForegroundColor Cyan
Write-Host "  cyan/waiting_video_in = empty Video In slot (link Live Input)"
Write-Host "  passthrough           = live blit (lime must keep moving)"
Select-String -Path $log -Pattern "STATUS stage=" | Select-Object -Last 15 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== key events ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "gpu_present|shared VIDEO_IN|STATUS stage=|live blit|ORT|MatteCapture|BgRemover" |
    Select-Object -Last 40 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== last 40 lines ====" -ForegroundColor Cyan
Get-Content $log -Tail 40

Write-Host ""
Write-Host "Expect LIVE camera with continuously moving lime heartbeat."
Write-Host "No ORT/MatteCaptureWorker lines (matte disabled in-process)."
Write-Host "Adapter LUID = PREVIEW engine GPU when testing preview."
