# GENESIS Remover - demo runbook (v63 debug harness)

**Goal:** Person Matte (RVM) cutout on a live camera in PIXERA at usable preview FPS.

**Stamp:** `remover-demo-v63f2-dll-stamp-verify-20260916`

**What changed**
- v62: CPU capture (host no longer Maps shared GPU texture - that froze preview)
- v63: thorough health-check + HEALTH/FAIL diagnostics so wrong installs are obvious
- v63e: output recover — abandon stuck async downloads, no black clear before masked present
- v63f: host stable — reuse MatteHost across Input recreate, rate-limit launches, never block UI on host exit (fixes ~15s hang when disabling outputs)

## Fast path (do this every time)

1. Quit PIXERA completely (Designer + Engine + Manager).
2. Repo:

```powershell
git fetch
git checkout cursor/remover-host-stable-7628
git pull
git lfs install
git lfs pull
```

3. Build + install + health check:

```powershell
.\BUILD-REMOVER.bat
.\INSTALL-PIXERA-REMOVER.bat
.\HEALTH-CHECK-REMOVER.bat
```

4. `HEALTH-CHECK-REMOVER.bat` must show `RESULT pass=N warn=N fail=0` and stamp `remover-demo-v63f2-dll-stamp-verify-*`.
5. If it fails, open `%TEMP%\ExtGenesisRemover.health.txt` and fix the FAIL lines before touching PIXERA.

## Models must be real blobs

```powershell
Get-Item assets\matting\rvm_mobilenetv3_fp16.onnx | Select-Object Length
# Expect ~7+ MB. If ~130 bytes -> LFS pointer; run git lfs pull then rebuild.
```

## PIXERA settings (demo defaults)

| Control | Value |
|---------|--------|
| Vision Effect | **Person Matte (RVM)** |
| Quality | **Fast** |
| Result As | **Alpha (key)** |
| Model File | leave **empty** |
| Camera Delay | **0** unless you need sync |
| Adapter LUID | **same GPU as PIXERA preview** (prefer NVIDIA) |

Debug Status should look like:

`remover-demo-v63f2-dll-stamp-verify-20260916 matte map=..ms infer=..ms ep=DML:N shm=1 matte=1`

- `shm=1` means CPU capture path (good)
- `MISMATCH` means mixed DLL/host - quit PIXERA and reinstall the whole folder

## Logs to watch

| File | What to look for |
|------|------------------|
| `%TEMP%\ExtGenesisRemover.log` | `[remover-demo-v63-...]` every line, `HEALTH ... shm=1`, `STATUS ...` |
| `%TEMP%\GenesisRemoverMatteHost.log` | `start remover-demo-v63-...`, `IDENTITY ...`, `capture_from_shm`, `HEALTH ...` |
| `%TEMP%\ExtGenesisRemover.FAIL.txt` | Should **not** exist; if present, read it first |
| `%TEMP%\ExtGenesisRemover.health.txt` | Last health-check report |

Dump quickly:

```powershell
.\scripts\dump-remover-log.ps1
.\scripts\health-check-remover.ps1
```

## Unhealthy / stop-and-fix

| Symptom | Meaning |
|---------|---------|
| Debug Status still v61/v62 | Old package still installed - rebuild+reinstall after quit PIXERA |
| `STAMP_MISMATCH` / FAIL file | Mixed DLL/host — quit PIXERA, `del %TEMP%\ExtGenesisRemover.FAIL.txt`, reinstall whole folder, reload DLL |
| Health check FAIL on RVM size | `git lfs pull` then rebuild/install |
| Cyan forever | Video In not linked |
| `passthrough` forever | MatteHost failed - read host log |
| Preview freezes every few seconds | Should be gone on v62+ (`shm=1`). If `shm=0`, wrong build |
| Matte flickers on real output (preview OK) | Need v63e+ (no clear-before-mask). Rebuild+reinstall |
| Preview freezes after disabling output | Need v63e+ (async abandon + host restart). Check log for `ABANDON` / `restartHost` |
| Build C2065 / parse errors | Pull latest output-recover branch; do not run half-built package |

## Real output (not just preview)

1. Same timeline as preview: ExtGenesisRemover + Live Input → Video In, Person Matte, Result As = Alpha.
2. Screens / Outputs: assign that layer to the physical / NDI output.
3. Put a plate under Remover so alpha reads clearly.
4. Toggle the output off/on once — preview must keep moving; Debug Status must stay on **v63f** with `shm=1 matte=1`.
5. If preview dies after toggle: dump logs (`.\scripts\dump-remover-log.ps1`) and look for `ABANDON` / `restartHost`.

## Demo flow (2 minutes)

1. Timeline: ExtGenesisRemover + Live Input -> Video In.
2. Confirm Debug Status stamp = **v63f** and `shm=1` `matte=1`.
3. Person walks in frame -> cutout; live camera stays smooth.
4. Route to a real output; cutout should stay stable (no flicker). Disable output — preview must recover.
5. Optional: Result As = Greyscale briefly to show matte, then back to Alpha.
