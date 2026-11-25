# Room OSC Particles (Tugela)

Dual-ring cylindrical room soft-particle demo driven by OSC person tracking. Particles-only install (no room/tank meshes).

## Scene

- Package: `assets/scenes/room_osc_particles.gns`
- Document: `assets/scenes/room_osc_particles.scene.json`
- Editor: **File → Examples → Room OSC Particles**

### Geometry (install scale)

| Element | Size | Motion |
|---------|------|--------|
| **Outer ring** | **~6 m diameter** (radius 3 m) | Soft Circles **rise / float upward** |
| **Middle / tank ring** | **~2 m diameter** (radius 1 m) | Soft Circles flow **outward on the floor** (no rise) |
| **Walk donut** | Between ~1 m and ~3 m radius | People track in this annulus |
| **Outer wall (math)** | radius 3 m | Center→person ray at fixed distance (default) |
| **Wall plane (optional)** | **one** user-placed Plane | Center→person ray ↔ that panel; miss → fixed radius |

### Tugela hierarchy (authored scene)

```
Particle System
├── Outer Ring          # ambient rise on 6 m ring
└── Inner Tank Ring     # ambient floor-outward on 2 m ring
Camera
```

**No pre-baked Person Pool.** When Protocol → **Enable room person** is on and `/genesis/room/person` messages arrive, Feet/Wall emitters are created **lazily** under `Scene/Person Pool` for active ids only. Idle pairs park at origin (disabled); they are not spawned just to fill slots. Build your own stack as you like — Duplicate / Shift+select Delete work in the Hierarchy.

## OSC addresses

Listen defaults: **UDP `0.0.0.0:9000`** (Protocol panel).

### `/genesis/room/person` (primary)

| Args | Meaning |
|------|---------|
| `,iff` `id x z` | **Preferred.** Person Int32 id + XZ floor meters. |
| `,ifff` `id x y z` | Id + feet height on Y. |
| `,iffff` `id x y z count` | As above; optional burst scale. |
| `,ff` `x z` | Legacy single-person → **id 0**. |
| `,fff` / `,ffff` | Legacy id 0 with optional Y / count. |

**Behavior:**

1. Map person **id** → slot (up to `max_persons`).
2. Create that slot’s **Feet** / **Wall** emitters on first message (if missing).
3. Move **Feet** to the person; cast center→person for **Wall** (fixed radius and/or one scene Plane).
4. After `slot_idle_seconds` with no messages, park existing emitters at origin (disabled).

### `/genesis/particles/emit` (still supported)

Generic XY tracker emit. Defaults can point at `Scene/Person Pool/Feet 00` once that leaf exists.

## Protocol settings

`OscRoomPersonSettings` (Protocol UI → **Room person**):

- **Enable room person** — off by default (blank launch stays empty)
- `max_persons` — max concurrent OSC ids that may create Feet+Wall pairs
- `slot_idle_seconds` — park timeout
- `room_radius` / `tank_radius` — 3 m / 1 m
- `wall_ray_mode` — Fixed distance | Plane collision | Prefer plane → distance
- `wall_plane_path` — Hierarchy path to Plane actor (default `Scene/Wall Plane`)
- `debug_draw_wall_ray` — viewport line + hit marker (off by default; editor-only)

**Hidden in Game:** actors expose an Unreal-like flag (Inspector → Hidden in Game; hierarchy badge `· HG`). Editor view draws them; Game View (`G`) and Pixera Renderbridge output skip their meshes. Collisions still run.

## Renderbridge notes

- ExtGenesis applies `applyRoomPerson` with per-stream cursors; loading `room_osc_particles` configures radii / listen — it does **not** bake a 10×2 emitter pool into the scene.
- Soft Circles motion updates run on GPU compute; spawn/OSC stay CPU-authored.
- Prefer `/genesis/room/person` `,iff` at 10–30 Hz for smooth tracking.
