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
    Write-Host "  Copy the WHOLE out\ExtGenesisRemover folder to:"
    Write-Host "    <PIXERA>\data\rxExt\ExtGenesisRemover\"
    Write-Host "  Do NOT put ExtGenesisRemover.dll inside data\rxExt\ExtGenesis\"
    Write-Host "  (that folder is for the scene bridge only)."
}
Write-Host ""
if ($ok) {
    Write-Host "Check passed. If PIXERA still fails, restart PIXERA after copying the folder." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Check failed. Fix the missing items above, rebuild with BUILD-AND-RUN.bat, retry." -ForegroundColor Red
    exit 1
}
