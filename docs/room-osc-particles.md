# Room OSC Particles

Dual-ring cylindrical room soft-particle demo driven by OSC person tracking. Stacked on the Niagara Soft Circles / Trail look work (`#38`).

## Scene

- Package: `assets/scenes/room_osc_particles.gns`
- Document: `assets/scenes/room_osc_particles.scene.json`
- Editor: **File → Examples → Room OSC Particles**

### Geometry (install scale)

Particles only — no room/tank meshes. Scale is authored in emitter spawn radii; the editor meter grid is the size reference.

| Element | Size | Notes |
|---------|------|--------|
| **Outer ring** | **~6 m diameter** (radius 3 m) | Ambient Soft Circles on the cylindrical room perimeter |
| **Inner tank ring** | **~2 m diameter** (radius 1 m) | Ambient Soft Circles at the central tank-base radius |
| **Walk donut** | Between ~1 m and ~3 m radius | People track in this annulus |
| **Outer wall (math)** | radius 3 m | OSC center→person→wall ray only — no wall mesh |

Black unlit stage (no scene lights); meter world grid + Unreal-like slower pan from editor chrome. Particles read as additive emissive glow with trail-behind wakes from the look tip.

### Niagara-like hierarchy

```
Particle System
├── Outer Ring         # ambient Soft Circles on 6 m diameter ring
├── Inner Tank Ring    # ambient Soft Circles on 2 m diameter tank base
├── Person Feet        # denser spawn at the tracked person (donut)
├── Wall Trace         # denser spawn at outer cylindrical wall hit
└── Center Ray         # burst samples along center → wall (OSC-driven)
```

## OSC addresses

Listen defaults: **UDP `0.0.0.0:9000`** (Protocol panel).

### `/genesis/room/person` (primary)

Floor person pose for the room effect. Keep the person in the **donut** (radius between tank and outer wall).

| Args | Meaning |
|------|---------|
| `,ff` `x z` | XZ floor meters, room-centered. Feet height uses settings default (`0.05`). |
| `,fff` `x y z` | Full position; `y` is feet height. |
| `,ffff` `x y z count` | As above; `count` scales feet/ray/wall bursts. |
| `,fff` `x z count` | If the third value is a large integer-ish number, it is treated as burst scale (same heuristic as `/genesis/particles/emit`). |

**Behavior on each message (deterministic with shared `scene_seed`):**

1. Move **Person Feet** emitter to the person (continuous rate + optional burst).
2. Cast a ray from **room center** through the person to the **cylindrical outer wall** (`room_radius`, default 3 m).
3. Burst soft particles along that ray (**Center Ray**).
4. Move **Wall Trace** to the wall hit and burst there.

### `/genesis/particles/emit` (still supported)

Generic XY tracker emit (see Protocol panel). On the room example it defaults to the Person Feet path so legacy streams still move feet without the wall/ray path.

## Protocol settings

`OscRoomPersonSettings` (Protocol UI → **Room person**):

- `room_radius` — outer cylindrical wall / outer ring radius (meters), default **3** (6 m diameter)
- `tank_radius` — inner tank ring radius (meters), default **1** (2 m diameter); authoring/docs
- `feet_burst` / `wall_burst` / `ray_samples` / `ray_burst_each`
- toggles: track feet/wall, burst feet/ray/wall

## Demo media

Store: `/cursor/stores/bc-bfd4a606-4e38-4cd5-93d8-8ae247b97bf2/media/dual_ring_room_*`

## Renderbridge notes

- ExtGenesis (`Input::render`) applies both `applyParticleEmits` and **`applyRoomPerson`** with per-stream cursors; loading `room_osc_particles` configures feet/wall/ray paths + 6 m/2 m radii.
- Soft Circles **motion updates run on GPU compute** (`particle_update.comp`); spawn/OSC bursts stay CPU-authored into the shared particle buffer. Trails sample after GPU readback.
- Same OSC stream + same scene + same `scene_seed` → matching sequences on dual GPU (shared OSC hub, per-consumer cursors).
- Prefer `/genesis/room/person` at a modest rate (e.g. 10–30 Hz) for smooth feet tracking without flooding bursts.
- Host dual-GPU visual parity still wants a Pixera box check when available.
