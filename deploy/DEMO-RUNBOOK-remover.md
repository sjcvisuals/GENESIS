# GENESIS Remover — Wednesday demo runbook (v62)

**Goal:** Person Matte (RVM) cutout on a live camera in PIXERA at usable preview FPS, with PIXERA staff watching.

**Stamp:** `remover-demo-v62-cpu-capture-20260913`

**v62 fix:** Matte host no longer Maps a shared GPU capture (that froze PIXERA to ~0.3 fps). DLL async-downloads a private RT into IPC; host only runs ORT.

## Before you leave for the venue

1. Quit PIXERA completely (Designer + Engine + Manager).
2. In the repo:

```powershell
git fetch
git checkout cursor/remover-demo-ready-7628   # or merge into your demo branch
git pull
git lfs install
git lfs pull
```

3. Confirm models are real blobs (not LFS text stubs):

```powershell
Get-Item assets\matting\rvm_mobilenetv3_fp16.onnx | Select-Object Length
# Expect ~7+ MB. If ~130 bytes → LFS pointer; stop and fix LFS.
```

4. Build + package:

```powershell
.\BUILD-REMOVER.bat
# or: powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

5. Install into PIXERA (Run as Administrator):

```powershell
.\INSTALL-PIXERA-REMOVER.bat
# or: .\scripts\install-pixera-remover.ps1
```

6. Install must print `BUILD_STAMP` with `remover-demo-v62-cpu-capture-*` and `RVM model OK`.

## PIXERA settings (demo defaults)

| Control | Value |
|---------|--------|
| Vision Effect | **Person Matte (RVM)** |
| Quality | **Fast** |
| Result As | **Alpha (key)** |
| Model File | leave **empty** |
| Camera Delay | **0** unless you need sync |
| Adapter LUID | **same GPU as PIXERA preview** (prefer NVIDIA; see host log) |

Leave Debug Status visible — it should read like:

`remover-demo-v62-cpu-capture-20260913 matte map=…ms infer=…ms ep=DML:N matte=1`

If you see `MISMATCH` → DLL and MatteHost are from different builds; quit PIXERA and reinstall the whole `ExtGenesisRemover` folder.

## Healthy log lines

**`%TEMP%\ExtGenesisRemover.log`**

- `[remover-demo-v62-cpu-capture-20260913]` on every line
- `MatteHost launched pid=…`
- `STATUS … stage=matte … mismatch=0`
- `gpu_present live+mask … map_ms=…` (map_ms ideally under ~35 under light load)
- Occasional `matte_cooldown` is OK — live frame keeps last matte

**`%TEMP%\GenesisRemoverMatteHost.log`**

- `start remover-demo-v62-cpu-capture-20260913`
- `RVM model OK bytes=…` (millions, not ~130)
- `BgRemover ready ep=DML:N` (CPU works but is slower)
- `matte_ok … map_ms=… infer_ms=… effect=Person Matte`
- Optional: `map_skip_reuse` when Map is expensive (intentional)

## Unhealthy / stop-and-fix

| Symptom | Meaning |
|---------|---------|
| Stamp still v60 / v56 / missing | Wrong DLL path or PIXERA still locking old copy |
| `STAMP_MISMATCH` | Mixed DLL/host — reinstall package |
| `RVM model is a Git LFS pointer` | `git lfs pull` then rebuild/install |
| `Map ONNX is a Git LFS pointer` | Vision model missing; stick to Person Matte for demo |
| Preview freezes hard | ORT must stay out-of-process (do not load ORT in the DLL) |
| Cyan forever | Video In not linked |
| `passthrough` forever, host never ready | MatteHost failed to start — check host log |
| map_ms stuck 40–80+ and preview stutters | Outputs fighting Map; v62 uses CPU capture (no host GPU Map); if still hitching, reduce capture size / outputs |

## Quick verify script

```powershell
.\scripts\dump-remover-log.ps1
.\scripts\check-pixera-dll.ps1
```

## Demo flow (2 minutes)

1. Timeline: ExtGenesisRemover + Live Input → Video In.
2. Confirm Debug Status stamp = v62 and `matte=1`.
3. Person walks in frame → cutout; live camera stays smooth (matte may update a few times/sec).
4. Optional: flip Result As to Greyscale briefly to show the matte, then back to Alpha.
