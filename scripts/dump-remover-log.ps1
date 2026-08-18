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
Write-Host "==== stamp (must be v32-fence-blit) ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "remover-timeline-" | ForEach-Object { $_.Line } |
    ForEach-Object { if ($_ -match '\[(remover-timeline-[^\]]+)\]') { $Matches[1] } } |
    Group-Object | Sort-Object Count -Descending |
    ForEach-Object { "{0,5}  {1}" -f $_.Count, $_.Name }

Write-Host ""
Write-Host "==== latest STATUS lines (stage machine) ====" -ForegroundColor Cyan
Write-Host "  cyan/waiting_video_in = empty Video In slot (link Live Input)"
Write-Host "  blue/download_fail    = fence wait or Map failed"
Write-Host "  orange/input_black    = Map zeros (check if preview still shows blit)"
Write-Host "  passthrough / matte   = working"
Select-String -Path $log -Pattern "STATUS stage=" | Select-Object -Last 15 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== key events ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "Device::before_render|SharedD3d11 ready|shared OUTPUT|video_in allocate|shared VIDEO_IN|gpu_blit|map_nz|STATUS stage=|matte_live|waiting Video In link|fence wait" |
    Select-Object -Last 35 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== last 40 lines ====" -ForegroundColor Cyan
Get-Content $log -Tail 40

Write-Host ""
Write-Host "Video In slot empty until you drag Live Input in = expected."
Write-Host "Set Adapter LUID to the PREVIEW engine GPU when testing preview; OUTPUT engine LUID for output."
Write-Host "Also check Debug Stage on the Remover resource."
