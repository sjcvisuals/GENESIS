# GENESIS branding (Windows icons)

| File | Use |
|------|-----|
| `genesis-editor.ico` | Embedded in `genesis_gltf_viewer.exe` (GENESIS Editor) |
| `genesis-player.ico` | Embedded in `genesis_player.exe` (GENESIS Player) |
| `genesis-*.svg` | Source marks (regenerate with the script below) |
| `genesis-*-256.png` | Preview / design reference |

Multi-size ICO contents: **16, 32, 48, 256**.

Regenerate after design edits:

```bash
python3 scripts/generate-app-icons.py
```

CMake (Windows) compiles `platform/genesis_version.rc.in` / `platform/genesis_player_version.rc.in` into the executables. Inno Setup uses `genesis-editor.ico` as `SetupIconFile`.
