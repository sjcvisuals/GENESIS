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
Write-Host "==== stamp (must be v30-rgba8) ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "remover-timeline-" | ForEach-Object { $_.Line } |
    ForEach-Object { if ($_ -match '\[(remover-timeline-[^\]]+)\]') { $Matches[1] } } |
    Group-Object | Sort-Object Count -Descending |
    ForEach-Object { "{0,5}  {1}" -f $_.Count, $_.Name }

Write-Host ""
Write-Host "==== latest STATUS lines (stage machine) ====" -ForegroundColor Cyan
Write-Host "  cyan/waiting_download = host Video In allocated, download not done yet"
Write-Host "  blue/download_fail    = download empty/abandoned"
Write-Host "  orange/input_black    = download ok but pixels are zero"
Write-Host "  passthrough / matte   = working (feed then cutout)"
Select-String -Path $log -Pattern "STATUS stage=" | Select-Object -Last 15 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== key events ====" -ForegroundColor Cyan
Select-String -Path $log -Pattern "FORCE format|keep RGBA8|video_in HOST|download armed|download_texture requested|download empty|download ABANDONED|STATUS stage=|matte_live|sampler host|opaque_passthrough|waiting_download" |
    Select-Object -Last 30 | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "==== last 40 lines ====" -ForegroundColor Cyan
Get-Content $log -Tail 40

Write-Host ""
Write-Host "Also check Debug Stage (enum) on the Remover resource - Debug Status string may lag in PIXERA."
