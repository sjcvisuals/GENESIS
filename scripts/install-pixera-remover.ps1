# Copy the WIP Remover package into PIXERA's data\rxExt folder (same place ExtGenesis lives).
#
# Must run elevated (Program Files) and with PIXERA fully quit (DLL unlock).
#
# Example:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-pixera-remover.ps1
#   Or double-click INSTALL-PIXERA-REMOVER.bat (will prompt for Admin)

param(
    [string]$PixeraRoot = "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73",
    [string]$SourceDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) {
    $SourceDir = Join-Path $RepoRoot "out\ExtGenesisRemover"
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    Write-Host "Re-launching install as Administrator (Program Files write access)..." -ForegroundColor Yellow
    $argList = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-PixeraRoot", "`"$PixeraRoot`""
    )
    if ($SourceDir) { $argList += @("-SourceDir", "`"$SourceDir`"") }
    $p = Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $p.ExitCode
}

$SourceDir = (Resolve-Path $SourceDir).Path
$dest = Join-Path $PixeraRoot "data\rxExt\ExtGenesisRemover"

if (-not (Test-Path (Join-Path $SourceDir "ExtGenesisRemover.dll"))) {
    throw "Missing ExtGenesisRemover.dll in $SourceDir - run BUILD-REMOVER.bat first"
}
if (-not (Test-Path (Join-Path $PixeraRoot "data\rxExt"))) {
    throw "PIXERA rxExt folder not found: $(Join-Path $PixeraRoot 'data\rxExt')"
}

# PIXERA locks DLLs while running.
$pixeraProcs = Get-Process -Name "Pixera*","RX*" -ErrorAction SilentlyContinue
if ($pixeraProcs) {
    Write-Host "PIXERA (or RX) is still running - quit it fully, then re-run install." -ForegroundColor Red
    $pixeraProcs | ForEach-Object { Write-Host ("  PID {0} {1}" -f $_.Id, $_.ProcessName) }
    throw "Quit PIXERA before installing Remover into Program Files."
}

Write-Host "Source: $SourceDir"
Write-Host "Dest:   $dest"

New-Item -ItemType Directory -Force -Path $dest | Out-Null

# Clear read-only bits that block overwrite under Program Files.
if (Test-Path $dest) {
    Get-ChildItem -Path $dest -Recurse -Force -ErrorAction SilentlyContinue |
        ForEach-Object {
            try { $_.Attributes = $_.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly) } catch { }
        }
}

# Mirror with robocopy (more reliable than Remove-Item under Program Files).
$robolog = Join-Path $env:TEMP "genesis-install-remover-robocopy.log"
$roboArgs = @(
    $SourceDir, $dest, "/MIR", "/R:2", "/W:1", "/NFL", "/NDL", "/NP", "/NJH", "/NJS",
    "/LOG:$robolog"
)
& robocopy @roboArgs | Out-Null
$roboCode = $LASTEXITCODE
# robocopy: 0-7 = success-ish, >=8 = failure
if ($roboCode -ge 8) {
    Write-Host "robocopy failed (exit $roboCode). Log: $robolog" -ForegroundColor Red
    if (Test-Path $robolog) { Get-Content $robolog -Tail 40 }
    throw "Install copy failed. Run as Admin and ensure PIXERA is fully quit."
}

Get-ChildItem -Path $dest -Recurse -Include *.dll,*.exe -ErrorAction SilentlyContinue |
    ForEach-Object {
        try { Unblock-File -Path $_.FullName -ErrorAction SilentlyContinue } catch { }
    }

$oldLog = Join-Path $env:TEMP "ExtGenesisRemover.log"
if (Test-Path $oldLog) {
    Remove-Item $oldLog -Force -ErrorAction SilentlyContinue
    Write-Host "Cleared old log: $oldLog"
}

$required = @(
    (Join-Path $dest "ExtGenesisRemover.dll"),
    (Join-Path $dest "onnxruntime.dll"),
    (Join-Path $dest "assets\matting\rvm_mobilenetv3_fp16.onnx")
)
foreach ($f in $required) {
    if (-not (Test-Path $f)) { throw "Install incomplete - missing $f" }
}

$ort = Get-Item (Join-Path $dest "onnxruntime.dll")
$ortVer = $ort.VersionInfo.FileVersion
Write-Host ""
Write-Host "Installed. In PIXERA:" -ForegroundColor Green
Write-Host "  1. Fully quit PIXERA (already required for this install)"
Write-Host "  2. Delete the old GENESIS Remover resource from the project (if present)"
Write-Host "  3. Re-import / browse to:"
Write-Host "       $(Join-Path $dest 'ExtGenesisRemover.dll')"
Write-Host "  4. Do NOT use the ExtGenesis folder for this DLL"
Write-Host ""
$dll = Get-Item (Join-Path $dest "ExtGenesisRemover.dll")
Write-Host ("DLL LastWriteTime: {0}" -f $dll.LastWriteTime)
Write-Host ("DLL Size:          {0:N0} bytes" -f $dll.Length)
Write-Host ("onnxruntime.dll:   {0}  (FileVersion={1})" -f $ort.FullName, $ortVer)
if ($ortVer -and ($ortVer -notmatch '^1\.2')) {
    Write-Host "WARNING: expected onnxruntime 1.20.x next to Remover (got $ortVer)." -ForegroundColor Yellow
    Write-Host "Rebuild with BUILD-REMOVER.bat so DirectML ORT 1.20 is synced into out\." -ForegroundColor Yellow
}
if (Test-Path (Join-Path $dest "BUILD_STAMP.txt")) {
    Write-Host "BUILD_STAMP.txt:"
    Get-Content (Join-Path $dest "BUILD_STAMP.txt") | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""
Write-Host "Color meanings while testing:"
Write-Host "  CYAN    = DLL rendering, waiting for Video In download"
Write-Host "  MAGENTA = Video In linked but pixels are empty"
Write-Host "  Webcam  = passthrough working; cutout follows after RVM warms up"
Write-Host ""
Write-Host "Stamp must be: remover-timeline-v9-dml-20260814"
Write-Host "Dump log with: .\scripts\dump-remover-log.ps1"
Write-Host ("Check DLL: .\scripts\check-pixera-dll.ps1 -DllPath `"{0}`"" -f (Join-Path $dest "ExtGenesisRemover.dll"))
if (Test-Path (Join-Path $dest "DirectML.dll")) {
    Write-Host "DirectML.dll: present (GPU RVM path)"
} else {
    Write-Host "DirectML.dll: MISSING (RVM will use CPU)" -ForegroundColor Yellow
}
