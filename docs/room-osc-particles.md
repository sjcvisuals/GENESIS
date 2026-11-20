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

### Tugela hierarchy

```
Particle System
├── Outer Ring              # ambient rise on 6 m ring
└── Inner Tank Ring         # ambient floor-outward on 2 m ring
Person Pool                 # 10 slots × Feet + Wall (constant load)
├── Feet 00 … Feet 09       # horizontal cylinder drift at feet (no rise)
└── Wall 00 … Wall 09       # rise at 3 m wall hit (like outer ring)
Camera
```

Idle person IDs park **both** Feet + Wall emitters at **world origin** (no spawn/despawn). Design capacity is 20 slots; install uses 10.

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

1. Map person **id** → pool slot (max 10 active).
2. Move that slot’s **Feet** emitter to the person (horizontal cylinder drift).
3. Cast center→person for the **Wall** emitter:
   - **Fixed distance** (default): hit at `room_radius` (~3 m).
   - **Plane collision** / **Prefer plane → distance**: intersect optional scene Plane (`Scene/Wall Plane` by default; mesh faces +Y — rotate upright for a wall). Miss falls back to fixed distance.
4. If no messages for `slot_idle_seconds` (~0.75 s), park both emitters at origin.

### `/genesis/particles/emit` (still supported)

Generic XY tracker emit. On the room example it defaults to `Scene/Person Pool/Feet 00`.

## Protocol settings

`OscRoomPersonSettings` (Protocol UI → **Room person**):

- `max_persons` — active slots (default 10; capacity 20)
- `slot_idle_seconds` — park timeout
- `room_radius` / `tank_radius` — 3 m / 1 m
- `wall_ray_mode` — Fixed distance | Plane collision | Prefer plane → distance
- `wall_plane_path` — Hierarchy path to Plane actor (default `Scene/Wall Plane`)
- `debug_draw_wall_ray` — viewport line + hit marker (off by default; editor-only)
- optional bursts (off by default; continuous emitter rate preferred)

**Hidden in Game:** actors expose an Unreal-like flag (Inspector → Hidden in Game; hierarchy badge `· HG`). Editor view draws them; Game View (`G`) and Pixera Renderbridge output skip their meshes. Collisions still run — the demo Wall Plane defaults Hidden in Game so wall particles hit/spawn while the plane stays invisible on output.

## Demo media

Store: `/cursor/stores/bc-bfd4a606-4e38-4cd5-93d8-8ae247b97bf2/media/` — `person_emit_*`, `hidden_in_game_{editor,game}.png`

## Renderbridge notes

- ExtGenesis (`Input::render`) applies `applyRoomPerson` with per-stream cursors; loading `room_osc_particles` configures the person pool + 6 m/2 m radii.
- Soft Circles **motion updates run on GPU compute** (`particle_update.comp`); spawn/OSC stay CPU-authored. Trails sample after GPU readback.
- Prefer `/genesis/room/person` `,iff` at 10–30 Hz for smooth multi-person tracking.
