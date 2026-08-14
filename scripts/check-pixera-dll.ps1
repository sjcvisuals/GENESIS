# Diagnose why PIXERA cannot LoadLibrary a GENESIS extension DLL.
#
# Usage (from repo root, after BUILD-AND-RUN.bat):
#   .\scripts\check-pixera-dll.ps1
#   .\scripts\check-pixera-dll.ps1 -DllPath .\out\ExtGenesisRemover\ExtGenesisRemover.dll

param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $DllPath) {
    $DllPath = Join-Path $RepoRoot "out\ExtGenesisRemover\ExtGenesisRemover.dll"
}
$DllPath = (Resolve-Path $DllPath).Path
$DllDir = Split-Path -Parent $DllPath

Write-Host ""
Write-Host "=== PIXERA DLL load check ===" -ForegroundColor Cyan
Write-Host "DLL:  $DllPath"
Write-Host "Dir:  $DllDir"
Write-Host ""

$required = @(
    @{ Path = $DllPath; Label = "extension DLL" },
    @{ Path = (Join-Path $DllDir "onnxruntime.dll"); Label = "onnxruntime.dll (must sit next to the extension)" },
    @{ Path = (Join-Path $DllDir "assets\matting\rvm_mobilenetv3_fp16.onnx"); Label = "RVM model" }
)

$ok = $true
foreach ($item in $required) {
    if (Test-Path $item.Path) {
        Write-Host ("[OK]  {0}" -f $item.Label) -ForegroundColor Green
    } else {
        Write-Host ("[MISS] {0}" -f $item.Label) -ForegroundColor Red
        Write-Host ("       {0}" -f $item.Path)
        $ok = $false
    }
}

$dml = Join-Path $DllDir "DirectML.dll"
if (Test-Path $dml) {
    Write-Host "[OK]  DirectML.dll (GPU RVM path)" -ForegroundColor Green
} else {
    Write-Host "[WARN] DirectML.dll missing - RVM falls back to CPU" -ForegroundColor Yellow
}

# Zone.Identifier (Mark-of-the-Web) can make LoadLibrary fail inside some hosts.
$zone = $DllPath + ":Zone.Identifier"
if (Test-Path $zone) {
    Write-Host "[WARN] Mark-of-the-Web Zone.Identifier present - unblocking..." -ForegroundColor Yellow
    try { Unblock-File -Path $DllPath } catch { }
}
Get-ChildItem -Path $DllDir -Filter *.dll -ErrorAction SilentlyContinue | ForEach-Object {
    try { Unblock-File -Path $_.FullName -ErrorAction SilentlyContinue } catch { }
}

# dumpbin dependents / exports when VS tools are available
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$dumpbin = $null
if (Test-Path $vswhere) {
    $dumpbin = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -find "**/Hostx64/x64/dumpbin.exe" 2>$null | Select-Object -First 1
}
if ($dumpbin -and (Test-Path $dumpbin)) {
    Write-Host ""
    Write-Host "--- dumpbin /dependents ---" -ForegroundColor Cyan
    & $dumpbin /dependents $DllPath | Select-String -Pattern "\.dll|Dump of|Image has"
    Write-Host ""
    Write-Host "--- dumpbin /exports (rxext_*) ---" -ForegroundColor Cyan
    & $dumpbin /exports $DllPath | Select-String -Pattern "rxext_|ordinal hint"
} else {
    Write-Host ""
    Write-Host "dumpbin not found - skipping PE dependents/exports check." -ForegroundColor Yellow
}

# Actual LoadLibrary from this process (same failure mode PIXERA hits)
Write-Host ""
Write-Host "--- LoadLibrary test ---" -ForegroundColor Cyan
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class GenesisNativeLoad {
  [DllImport("kernel32", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);
  [DllImport("kernel32", SetLastError=true)]
  public static extern bool FreeLibrary(IntPtr hModule);
  [DllImport("kernel32", SetLastError=true, CharSet=CharSet.Ansi)]
  public static extern IntPtr GetProcAddress(IntPtr hModule, string procName);
  [DllImport("kernel32")]
  public static extern uint GetLastError();
  public const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x00000008;
}
"@

# Prefer loading with altered search path so sibling onnxruntime.dll resolves.
$handle = [GenesisNativeLoad]::LoadLibraryEx($DllPath, [IntPtr]::Zero,
    [GenesisNativeLoad]::LOAD_WITH_ALTERED_SEARCH_PATH)
if ($handle -eq [IntPtr]::Zero) {
    $err = [GenesisNativeLoad]::GetLastError()
    Write-Host ("[FAIL] LoadLibraryEx failed. Win32 error = {0}" -f $err) -ForegroundColor Red
    Write-Host "Common causes:"
    Write-Host "  126 = missing dependency (often onnxruntime.dll or VC++ redist)"
    Write-Host "  193 = not a valid Win32 app (wrong arch / corrupt file)"
    Write-Host "Tip: open the DLL folder in Explorer and confirm onnxruntime.dll is present."
    $ok = $false
} else {
    Write-Host "[OK]  LoadLibraryEx succeeded" -ForegroundColor Green
    foreach ($sym in @("rxext_open", "rxext_close")) {
        $proc = [GenesisNativeLoad]::GetProcAddress($handle, $sym)
        if ($proc -eq [IntPtr]::Zero) {
            Write-Host ("[FAIL] Missing export: {0}" -f $sym) -ForegroundColor Red
            $ok = $false
        } else {
            Write-Host ("[OK]  export {0}" -f $sym) -ForegroundColor Green
        }
    }
    [void][GenesisNativeLoad]::FreeLibrary($handle)
}

Write-Host ""
if ($DllPath -match 'ExtGenesisRemover') {
    Write-Host "PIXERA install tip:" -ForegroundColor Cyan
    Write-Host "  ExtGenesis works for you from data\rxExt\ExtGenesis - install Remover the same way:"
    Write-Host "    .\scripts\install-pixera-remover.ps1"
    Write-Host "  That copies out\ExtGenesisRemover -> <PIXERA>\data\rxExt\ExtGenesisRemover\"
    Write-Host "  Then browse PIXERA to THAT ExtGenesisRemover.dll (not the ExtGenesis folder)."

    $sibling = Join-Path $RepoRoot "out\ExtGenesis\ExtGenesis.dll"
    if (Test-Path $sibling) {
        Write-Host ""
        Write-Host "Comparing LoadLibrary with working ExtGenesis from out\ ..." -ForegroundColor Cyan
        $h2 = [GenesisNativeLoad]::LoadLibraryEx($sibling, [IntPtr]::Zero,
            [GenesisNativeLoad]::LOAD_WITH_ALTERED_SEARCH_PATH)
        if ($h2 -eq [IntPtr]::Zero) {
            Write-Host ("[WARN] out\ExtGenesis also failed LoadLibrary (err {0}) - your working Pixera copy is probably the older data\rxExt one." -f ([GenesisNativeLoad]::GetLastError())) -ForegroundColor Yellow
        } else {
            Write-Host "[OK]  out\ExtGenesis\ExtGenesis.dll loads in this process" -ForegroundColor Green
            [void][GenesisNativeLoad]::FreeLibrary($h2)
        }
    }
}
Write-Host ""
if ($ok) {
    Write-Host "Check passed. If PIXERA still fails, restart PIXERA after copying the folder." -ForegroundColor Green
    if ($MyInvocation.InvocationName -ne '.') {
        exit 0
    }
} else {
    Write-Host "Check failed. Fix the missing items above, rebuild with BUILD-AND-RUN.bat, retry." -ForegroundColor Red
    if ($MyInvocation.InvocationName -ne '.') {
        exit 1
    }
    throw "PIXERA DLL load check failed"
}
