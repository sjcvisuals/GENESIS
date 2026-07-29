# Genesis Remover — free PIXERA install

**Genesis Remover** is the free, matte-only fork of GENESIS for PIXERA:
realtime person cutout via the generic RenderBridge extension. No editor,
no scenes, no NVIDIA Broadcast SDK.

PIXERA owns live SDI / NDI / media ingest. You drag that resource onto
**Video In**; the extension outputs a transparent RGBA person cutout.

## Free download

Publish these artifacts from a Windows Release build (see packaging below)
as GitHub Release assets so anyone can grab a link:

| Artifact | Use |
|----------|-----|
| `GenesisRemoverSetup-0.1.0.exe` | One-click Windows installer (VC++ redist + PIXERA `rxExt` path) |
| `GenesisRemover-0.1.0.zip` | Portable folder — unzip and browse to the DLL |

## End-user install (Setup.exe)

1. Download and run `GenesisRemoverSetup-*.exe`.
2. Choose **Install into PIXERA** and select your PIXERA root  
   (e.g. `C:\Program Files\AV Stumpfl\PIXERA` or `C:\PIXERA`).
3. Finish the wizard (VC++ redistributable installs if needed).
4. Module path:
   ```
   <PIXERA>\data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll
   ```

### Portable path

1. Unzip `GenesisRemover-*.zip` anywhere (keep the folder intact).
2. In PIXERA, browse the generic extension picker to `ExtGenesisRemover.dll`.

## Wire it in PIXERA

1. **Compositing** → add a **generic RenderBridge extension** layer.
2. Browse to `ExtGenesisRemover.dll`.
3. Drag your **SDI / NDI / media** resource onto **Video In**.
4. Set **Quality** (`Fast` / `Balanced` / `Quality`).
5. Use the layer sampler as a transparent cutout over your show.

Keep these siblings next to the DLL: `onnxruntime.dll`,
`assets/matting/rvm_mobilenetv3_fp16.onnx`, `GenesisRemover.manifest.json`.

## Requirements (playback machine)

| Need | Notes |
|------|--------|
| Windows 10/11 x64 | |
| PIXERA + RenderBridge API **1.2** | API mismatch → rebuild against vendored SDK |
| GPU driver with Vulkan 1.1+ | Vendor driver; no Vulkan SDK install |
| VC++ 2015–2022 x64 | Bundled by Setup.exe |

**Not required:** Genesis Editor, CMake, MSVC, NVIDIA Broadcast / Maxine.

## Developer packaging (Windows)

```powershell
# Slim Remover-only CMake tree + installer + ZIP
.\scripts\package-remover-installer.ps1

# Or build DLL only, then package
.\scripts\build.ps1 -RemoverOnly -Config Release
.\scripts\package-remover-installer.ps1 -SkipBuild
```

Remover-only configure:

```powershell
cmake -B build -DGENESIS_REMOVER_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ExtGenesisRemover
```

Outputs:

```
build\package\GenesisRemoverSetup-0.1.0.exe
build\package\GenesisRemover-0.1.0.zip
build\deploy\GenesisRemover\          # ready-to-copy folder
```

Inno Setup 6 is required for the `.exe` wizard; without it the script still
writes the portable ZIP.

## Architecture

See [video-bg-remover.md](video-bg-remover.md): shared `RemoverPipeline` /
RVM ONNX / async matte worker. Full GENESIS (`ExtGenesis`) can also run
Background Remover mode; this free module is cutout-only.
