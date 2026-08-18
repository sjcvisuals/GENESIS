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
Write-Host "==== stamp (must be v36-blit-heartbeat) ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "remover-timeline-" | ForEach-Object { $_.Line } |
    ForEach-Object { if ($_ -match '\[(remover-timeline-[^\]]+)\]') { $Matches[1] } } |
    Group-Object | Sort-Object Count -Descending |
    ForEach-Object { "{0,5}  {1}" -f $_.Count, $_.Name }

Write-Host ""
Write-Host "==== latest STATUS lines (stage machine) ====" -ForegroundColor Cyan
Write-Host "  cyan/waiting_video_in = empty Video In slot (link Live Input)"
Write-Host "  blue/download_fail    = live path missing RTV/SRV"
Write-Host "  passthrough           = blit+heartbeat (no matte in v36)"
Select-String -Path $log -Pattern "STATUS stage=" | Select-Object -Last 15 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== key events ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "Device::before_render|shared VIDEO_IN|gpu_blit|heartbeat|STATUS stage=|waiting Video In link|video_in cleared" |
    Select-Object -Last 35 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== last 40 lines ====" -ForegroundColor Cyan
Get-Content $log -Tail 40

Write-Host ""
Write-Host "v36 is blit-only. Watch the LIME stripe on the Remover output:"
Write-Host "  stripe MOVES + face frozen  => Video In stuck (PIXERA wrote once)"
Write-Host "  stripe ALSO frozen          => PIXERA not presenting our shared output"
Write-Host "  stripe MOVES + face live    => feed OK; next build re-enables matte"
Write-Host "Adapter LUID = PREVIEW engine GPU when testing preview."
