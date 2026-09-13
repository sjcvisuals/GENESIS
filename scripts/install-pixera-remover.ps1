# Copy the WIP Remover package into PIXERA's data\rxExt folder (same place ExtGenesis lives).
#
# Must run elevated (Program Files) and with PIXERA fully quit (DLL unlock).
#
# Example:
#   Right-click INSTALL-PIXERA-REMOVER.bat -> Run as administrator
#   OR:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-pixera-remover.ps1

param(
    [string]$PixeraRoot = "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73",
    [string]$SourceDir = "",
    [switch]$NoElevate
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$LogPath = Join-Path $env:TEMP "genesis-install-remover.log"

function Write-InstallLog([string]$Message, [string]$Color = "White") {
    $line = "{0} {1}" -f (Get-Date -Format "o"), $Message
    Add-Content -Path $LogPath -Value $line -Encoding UTF8
    if ($Color -eq "White") { Write-Host $Message }
    else { Write-Host $Message -ForegroundColor $Color }
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

try {
    Set-Content -Path $LogPath -Value ("=== GENESIS Remover install log {0} ===" -f (Get-Date -Format o)) -Encoding UTF8
    Write-InstallLog ("RepoRoot={0}" -f $RepoRoot)
    Write-InstallLog ("PSCommandPath={0}" -f $PSCommandPath)
    Write-InstallLog ("Admin={0}" -f (Test-IsAdmin))

    if (-not $SourceDir) {
        $SourceDir = Join-Path $RepoRoot "out\ExtGenesisRemover"
    }

    if (-not (Test-IsAdmin) -and -not $NoElevate) {
        Write-InstallLog "Not admin - launching elevated copy..." "Yellow"
        # One argument string: Start-Process array quoting breaks paths with spaces.
        $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -PixeraRoot `"$PixeraRoot`" -SourceDir `"$SourceDir`" -NoElevate"
        try {
            $p = Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $arg -Wait -PassThru
        } catch {
            Write-InstallLog ("UAC elevation failed/cancelled: {0}" -f $_.Exception.Message) "Red"
            Write-InstallLog "Fix: right-click INSTALL-PIXERA-REMOVER.bat -> Run as administrator" "Yellow"
            Write-InstallLog ("Full log: {0}" -f $LogPath) "Yellow"
            exit 1
        }
        if (-not $p) {
            Write-InstallLog "UAC elevation returned no process (cancelled?)." "Red"
            exit 1
        }
        Write-InstallLog ("Elevated process exit code={0}" -f $p.ExitCode)
        Write-InstallLog ("Full log: {0}" -f $LogPath)
        exit $p.ExitCode
    }

    if (-not (Test-IsAdmin)) {
        Write-InstallLog "Still not Administrator after elevation attempt." "Red"
        Write-InstallLog "Right-click INSTALL-PIXERA-REMOVER.bat -> Run as administrator" "Yellow"
        exit 1
    }

    if (-not (Test-Path $SourceDir)) {
        throw "Source folder missing: $SourceDir - run BUILD-REMOVER.bat first"
    }
    $SourceDir = (Resolve-Path $SourceDir).Path
    $dest = Join-Path $PixeraRoot "data\rxExt\ExtGenesisRemover"

    if (-not (Test-Path (Join-Path $SourceDir "ExtGenesisRemover.dll"))) {
        throw "Missing ExtGenesisRemover.dll in $SourceDir - run BUILD-REMOVER.bat first"
    }
    if (-not (Test-Path (Join-Path $PixeraRoot "data\rxExt"))) {
        throw "PIXERA rxExt folder not found: $(Join-Path $PixeraRoot 'data\rxExt')"
    }

    # Only block on real PIXERA host processes (not every RX*).
    $pixeraProcs = @()
    foreach ($n in @("Pixera", "PixeraDesigner", "PixeraEngine", "PixeraManager")) {
        $pixeraProcs += @(Get-Process -Name $n -ErrorAction SilentlyContinue)
    }
    if ($pixeraProcs.Count -gt 0) {
        Write-InstallLog "PIXERA is still running - quit it fully, then re-run install." "Red"
        $pixeraProcs | ForEach-Object { Write-InstallLog ("  PID {0} {1}" -f $_.Id, $_.ProcessName) "Red" }
        throw "Quit PIXERA before installing Remover into Program Files."
    }

    Write-InstallLog ("Source: {0}" -f $SourceDir) "Cyan"
    Write-InstallLog ("Dest:   {0}" -f $dest) "Cyan"

    New-Item -ItemType Directory -Force -Path $dest | Out-Null

    if (Test-Path $dest) {
        Get-ChildItem -Path $dest -Recurse -Force -ErrorAction SilentlyContinue |
            ForEach-Object {
                try { $_.Attributes = $_.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly) } catch { }
            }
    }

    $robolog = Join-Path $env:TEMP "genesis-install-remover-robocopy.log"
    $roboArgs = @(
        $SourceDir, $dest, "/E", "/IS", "/IT", "/R:2", "/W:1", "/NFL", "/NDL", "/NP", "/NJH", "/NJS",
        "/LOG:$robolog"
    )
    Write-InstallLog ("robocopy {0}" -f ($roboArgs -join " "))
    & robocopy @roboArgs | Out-Null
    $roboCode = $LASTEXITCODE
    Write-InstallLog ("robocopy exit={0}" -f $roboCode)
    # 0-7 success-ish; >=8 failure. /MIR can fail on locked extras - /E overwrite is safer.
    if ($roboCode -ge 8) {
        if (Test-Path $robolog) {
            Get-Content $robolog -Tail 50 | ForEach-Object { Write-InstallLog $_ }
        }
        throw "robocopy failed (exit $roboCode). Is PIXERA quit? Run bat as Administrator."
    }

    Get-ChildItem -Path $dest -Recurse -Include *.dll,*.exe -ErrorAction SilentlyContinue |
        ForEach-Object {
            try { Unblock-File -Path $_.FullName -ErrorAction SilentlyContinue } catch { }
        }

    $oldLog = Join-Path $env:TEMP "ExtGenesisRemover.log"
    if (Test-Path $oldLog) {
        Remove-Item $oldLog -Force -ErrorAction SilentlyContinue
        Write-InstallLog ("Cleared old log: {0}" -f $oldLog)
    }

    $required = @(
        (Join-Path $dest "ExtGenesisRemover.dll"),
        (Join-Path $dest "GenesisRemoverMatteHost.exe"),
        (Join-Path $dest "onnxruntime.dll"),
        (Join-Path $dest "assets\matting\rvm_mobilenetv3_fp16.onnx")
    )
    foreach ($f in $required) {
        if (-not (Test-Path $f)) { throw "Install incomplete - missing $f" }
    }

    $rvm = Join-Path $dest "assets\matting\rvm_mobilenetv3_fp16.onnx"
    $rvmLen = (Get-Item $rvm).Length
    if ($rvmLen -lt 1000000) {
        $head = Get-Content -LiteralPath $rvm -TotalCount 2 -ErrorAction SilentlyContinue
        throw ("RVM model is not a real ONNX blob ({0:N0} bytes). Likely Git LFS pointer. Run: git lfs pull — then BUILD + install again. Head: {1}" -f $rvmLen, ($head -join ' | '))
    }
    Write-InstallLog ("RVM model OK: {0:N1} MB" -f ($rvmLen / 1MB)) "Green"

    $visionModels = @(
        "depth_anything_v2_vits.onnx",
        "birefnet_512_fp16.onnx",
        "u2net_saliency.onnx",
        "human_parse.onnx",
        "style_mosaic.onnx"
    )
    $visionDir = Join-Path $dest "assets\vision"
    $visionOk = 0
    $visionBad = 0
    foreach ($name in $visionModels) {
        $vm = Join-Path $visionDir $name
        if (-not (Test-Path $vm)) {
            Write-InstallLog ("WARNING: missing vision model {0} (Person Matte demo still OK)" -f $vm) "Yellow"
            $visionBad++
            continue
        }
        $len = (Get-Item $vm).Length
        if ($len -lt 1000000) {
            Write-InstallLog ("ERROR: {0} is only {1:N0} bytes — Git LFS pointer. Run: git lfs pull then rebuild/install" -f $vm, $len) "Red"
            $visionBad++
        } else {
            Write-InstallLog ("vision model OK: {0} ({1:N1} MB)" -f $name, ($len / 1MB))
            $visionOk++
        }
    }
    if ($visionBad -gt 0) {
        Write-InstallLog ("Vision assets incomplete ({0} OK / {1} bad). Wednesday demo uses Person Matte (RVM) — still OK if RVM passed." -f $visionOk, $visionBad) "Yellow"
    }

    $ort = Get-Item (Join-Path $dest "onnxruntime.dll")
    $ortVer = $ort.VersionInfo.FileVersion
    $dll = Get-Item (Join-Path $dest "ExtGenesisRemover.dll")

    Write-InstallLog "Installed OK." "Green"
    Write-InstallLog ("DLL LastWriteTime: {0}" -f $dll.LastWriteTime)
    Write-InstallLog ("DLL Size:          {0:N0} bytes" -f $dll.Length)
    Write-InstallLog ("MatteHost:         {0}" -f (Join-Path $dest "GenesisRemoverMatteHost.exe"))
    Write-InstallLog ("onnxruntime.dll FileVersion={0}" -f $ortVer)
    if ($ortVer -and ($ortVer -notmatch '^1\.2')) {
        Write-InstallLog ("WARNING: expected onnxruntime 1.20.x (got {0})" -f $ortVer) "Yellow"
    }
    if (Test-Path (Join-Path $dest "BUILD_STAMP.txt")) {
        Write-InstallLog "BUILD_STAMP.txt:"
        Get-Content (Join-Path $dest "BUILD_STAMP.txt") | ForEach-Object { Write-InstallLog ("  {0}" -f $_) }
        $stampText = Get-Content (Join-Path $dest "BUILD_STAMP.txt") -Raw
        if ($stampText -notmatch 'remover-demo-v61') {
            Write-InstallLog "WARNING: BUILD_STAMP does not contain remover-demo-v61 — wrong package?" "Red"
        }
    } else {
        Write-InstallLog "WARNING: BUILD_STAMP.txt missing in package" "Yellow"
    }
    if (Test-Path (Join-Path $dest "DirectML.dll")) {
        Write-InstallLog "DirectML.dll: present (host may use DML; DLL does not load ORT)" "Green"
    } else {
        Write-InstallLog "DirectML.dll: MISSING (host uses CPU ORT)" "Yellow"
    }

    Write-Host ""
    Write-Host "In PIXERA:" -ForegroundColor Green
    Write-Host "  Re-import:"
    Write-Host "    $(Join-Path $dest 'ExtGenesisRemover.dll')"
    Write-Host "  Stamp must be: remover-demo-v61-live-fps-*"
    Write-Host "  Demo defaults: Vision Effect=Person Matte (RVM), Quality=Fast, Result As=Alpha"
    Write-Host "  Debug Status should show: stamp + map_ms + infer_ms + ep"
    Write-Host "  Matte host auto-launches; log: %TEMP%\GenesisRemoverMatteHost.log"
    Write-Host ("Install log: {0}" -f $LogPath)
    exit 0
}
catch {
    Write-InstallLog ("ERROR: {0}" -f $_.Exception.Message) "Red"
    Write-InstallLog ("Full log: {0}" -f $LogPath) "Yellow"
    exit 1
}
