# GENESIS Remover - portable installer (no git repo required).
# Lives next to package\ExtGenesisRemover.dll inside the zip.
#
# Usage:
#   Right-click INSTALL.bat -> Run as administrator
#   OR:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
#   powershell ... -File .\install.ps1 -PixeraRoot "D:\Pixera\build_25-3_INTER_73"

param(
    [string]$PixeraRoot = "",
    [string]$SourceDir = "",
    [switch]$NoElevate,
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$LogPath = Join-Path $env:TEMP "genesis-install-remover-portable.log"

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

function Resolve-PackageDir([string]$Hint) {
    $candidates = @()
    if ($Hint) { $candidates += $Hint }
    $candidates += @(
        (Join-Path $Here "package"),
        (Join-Path $Here "ExtGenesisRemover"),
        $Here
    )
    foreach ($c in $candidates) {
        if (-not $c) { continue }
        $dll = Join-Path $c "ExtGenesisRemover.dll"
        if (Test-Path -LiteralPath $dll) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    return $null
}

function Find-PixeraRoots {
    $roots = New-Object System.Collections.Generic.List[string]
    $bases = @(
        (Join-Path ${env:ProgramFiles} "AV Stumpfl\Pixera"),
        (Join-Path ${env:ProgramFiles(x86)} "AV Stumpfl\Pixera")
    )
    foreach ($base in $bases) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $rx = Join-Path $_.FullName "data\rxExt"
            if (Test-Path -LiteralPath $rx) {
                [void]$roots.Add($_.FullName)
            }
        }
    }
    return @($roots | Sort-Object -Unique)
}

function Browse-Folder([string]$Description) {
    Add-Type -AssemblyName System.Windows.Forms | Out-Null
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = $Description
    $dlg.ShowNewFolderButton = $false
    $dlg.RootFolder = [System.Environment+SpecialFolder]::MyComputer
    $result = $dlg.ShowDialog()
    if ($result -eq [System.Windows.Forms.DialogResult]::OK) {
        return $dlg.SelectedPath
    }
    return $null
}

function Test-PixeraRoot([string]$Root) {
    if (-not $Root) { return $false }
    $rx = Join-Path $Root "data\rxExt"
    return (Test-Path -LiteralPath $rx)
}

function Select-PixeraRootInteractive {
    Write-Host ""
    Write-Host "=== Choose PIXERA install folder ===" -ForegroundColor Cyan
    Write-Host "Need the folder that contains data\rxExt (example:"
    Write-Host "  C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73"
    Write-Host ")"
    Write-Host ""

    $found = @(Find-PixeraRoots)
    $i = 1
    $map = @{}
    foreach ($r in $found) {
        Write-Host ("  [{0}] {1}" -f $i, $r)
        $map[$i] = $r
        $i++
    }
    Write-Host ("  [{0}] Browse for folder..." -f $i)
    $browseIdx = $i
    $i++
    Write-Host ("  [{0}] Type a path" -f $i)
    $typeIdx = $i

    Write-Host ""
    $choice = Read-Host "Enter number"
    if ($choice -match '^\d+$') {
        $n = [int]$choice
        if ($map.ContainsKey($n)) { return $map[$n] }
        if ($n -eq $browseIdx) {
            $picked = Browse-Folder "Select PIXERA root (folder that contains data\rxExt)"
            if ($picked) { return $picked }
            throw "Folder browse cancelled."
        }
        if ($n -eq $typeIdx) {
            $typed = Read-Host "PIXERA root path"
            return $typed.Trim('"')
        }
    }
    throw ("Invalid choice: {0}" -f $choice)
}

function Get-EmbeddedStamps([string]$DllPath) {
    if (-not (Test-Path -LiteralPath $DllPath)) { return @() }
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $DllPath))
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    return @([regex]::Matches($text, 'remover-demo-v[0-9][a-z0-9\-]+') |
        ForEach-Object { $_.Value } | Sort-Object -Unique)
}

try {
    Set-Content -Path $LogPath -Value ("=== GENESIS Remover portable install {0} ===" -f (Get-Date -Format o)) -Encoding UTF8
    Write-InstallLog ("InstallerDir={0}" -f $Here)
    Write-InstallLog ("Admin={0}" -f (Test-IsAdmin))

    $SourceDir = Resolve-PackageDir $SourceDir
    if (-not $SourceDir) {
        throw "Could not find ExtGenesisRemover.dll. Expected package\ next to INSTALL.bat (or run from the unzipped folder)."
    }
    Write-InstallLog ("Package: {0}" -f $SourceDir) "Cyan"

    if (-not $PixeraRoot) {
        if ($NoPrompt) {
            throw "PixeraRoot not set. Pass -PixeraRoot or run without -NoPrompt."
        }
        $PixeraRoot = Select-PixeraRootInteractive
    }
    $PixeraRoot = $PixeraRoot.Trim().Trim('"')
    if (-not (Test-PixeraRoot $PixeraRoot)) {
        throw ("Not a PIXERA root (missing data\rxExt): {0}" -f $PixeraRoot)
    }
    $PixeraRoot = (Resolve-Path -LiteralPath $PixeraRoot).Path
    Write-InstallLog ("PIXERA:  {0}" -f $PixeraRoot) "Cyan"

    if (-not (Test-IsAdmin) -and -not $NoElevate) {
        Write-InstallLog "Not admin - launching elevated copy..." "Yellow"
        $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -PixeraRoot `"$PixeraRoot`" -SourceDir `"$SourceDir`" -NoElevate -NoPrompt"
        try {
            $p = Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $arg -Wait -PassThru
        } catch {
            Write-InstallLog ("UAC elevation failed/cancelled: {0}" -f $_.Exception.Message) "Red"
            Write-InstallLog "Fix: right-click INSTALL.bat -> Run as administrator" "Yellow"
            exit 1
        }
        if (-not $p) { exit 1 }
        Write-InstallLog ("Elevated process exit code={0}" -f $p.ExitCode)
        Write-InstallLog ("Full log: {0}" -f $LogPath)
        exit $p.ExitCode
    }

    if (-not (Test-IsAdmin)) {
        throw "Still not Administrator. Right-click INSTALL.bat -> Run as administrator."
    }

    $pixeraProcs = @()
    foreach ($n in @("Pixera", "PixeraDesigner", "PixeraEngine", "PixeraManager")) {
        $pixeraProcs += @(Get-Process -Name $n -ErrorAction SilentlyContinue)
    }
    if ($pixeraProcs.Count -gt 0) {
        $pixeraProcs | ForEach-Object { Write-InstallLog ("  PID {0} {1}" -f $_.Id, $_.ProcessName) "Red" }
        throw "Quit PIXERA fully before installing."
    }

    $dest = Join-Path $PixeraRoot "data\rxExt\ExtGenesisRemover"
    Write-InstallLog ("Dest:    {0}" -f $dest) "Cyan"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null

    Get-ChildItem -Path $dest -Recurse -Force -ErrorAction SilentlyContinue |
        ForEach-Object {
            try { $_.Attributes = $_.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly) } catch { }
        }

    $robolog = Join-Path $env:TEMP "genesis-install-remover-portable-robocopy.log"
    $roboArgs = @(
        $SourceDir, $dest, "/E", "/IS", "/IT", "/R:2", "/W:1", "/NFL", "/NDL", "/NP", "/NJH", "/NJS",
        "/LOG:$robolog"
    )
    Write-InstallLog ("robocopy {0}" -f ($roboArgs -join " "))
    & robocopy @roboArgs | Out-Null
    $roboCode = $LASTEXITCODE
    Write-InstallLog ("robocopy exit={0}" -f $roboCode)
    if ($roboCode -ge 8) {
        if (Test-Path $robolog) {
            Get-Content $robolog -Tail 40 | ForEach-Object { Write-InstallLog $_ }
        }
        throw "robocopy failed (exit $roboCode)."
    }

    Get-ChildItem -Path $dest -Recurse -Include *.dll,*.exe -ErrorAction SilentlyContinue |
        ForEach-Object {
            try { Unblock-File -Path $_.FullName -ErrorAction SilentlyContinue } catch { }
        }

    $required = @(
        (Join-Path $dest "ExtGenesisRemover.dll"),
        (Join-Path $dest "GenesisRemoverMatteHost.exe"),
        (Join-Path $dest "onnxruntime.dll"),
        (Join-Path $dest "assets\matting\rvm_mobilenetv3_fp16.onnx")
    )
    foreach ($f in $required) {
        if (-not (Test-Path -LiteralPath $f)) { throw "Install incomplete - missing $f" }
    }

    $rvm = Join-Path $dest "assets\matting\rvm_mobilenetv3_fp16.onnx"
    $rvmLen = (Get-Item -LiteralPath $rvm).Length
    if ($rvmLen -lt 1000000) {
        throw ("RVM model too small ({0:N0} bytes) - bad package / LFS pointer." -f $rvmLen)
    }

    $dll = Join-Path $dest "ExtGenesisRemover.dll"
    $stamps = @(Get-EmbeddedStamps $dll)
    if ($stamps.Count -eq 0) {
        Write-InstallLog "WARNING: DLL has no remover-demo stamp embedded" "Yellow"
    } else {
        Write-InstallLog ("DLL binary stamp(s): {0}" -f ($stamps -join ', ')) "Green"
    }

    foreach ($old in @(
            (Join-Path $env:TEMP "ExtGenesisRemover.FAIL.txt"),
            (Join-Path $env:TEMP "ExtGenesisRemover.log")
        )) {
        if (Test-Path -LiteralPath $old) {
            Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue
            Write-InstallLog ("Cleared: {0}" -f $old)
        }
    }

    Write-InstallLog "Installed OK." "Green"
    Write-Host ""
    Write-Host "In PIXERA:" -ForegroundColor Green
    Write-Host "  1. Quit was required (already done)."
    Write-Host "  2. Browse / re-import THIS DLL (not ExtGenesis, not a Documents copy):"
    Write-Host ("     {0}" -f $dll)
    Write-Host "  3. Debug Status stamp must match the DLL binary stamp above."
    Write-Host "  4. Live log:  %TEMP%\ExtGenesisRemover.log"
    Write-Host "  5. Host log:  %TEMP%\GenesisRemoverMatteHost.log"
    Write-Host ("Install log: {0}" -f $LogPath)
    exit 0
}
catch {
    Write-InstallLog ("ERROR: {0}" -f $_.Exception.Message) "Red"
    Write-InstallLog ("Full log: {0}" -f $LogPath) "Yellow"
    exit 1
}
