# Room OSC Particles

Room-scale soft-particle demo driven by OSC person tracking. Stacked on the Niagara Soft Circles / Trail look work (`#38`).

## Scene

- Package: `assets/scenes/room_osc_particles.gns`
- Document: `assets/scenes/room_osc_particles.scene.json`
- Editor: **File → Examples → Room OSC Particles**

### Niagara-like hierarchy

```
Particle System
├── Ambient Field     # sparse soft circles rising across the room
├── Person Feet       # denser spawn at the tracked person
├── Wall Trace        # denser spawn at the wall hit in front of them
└── Center Ray        # burst samples along center → wall (OSC-driven)
```

Room AABB defaults to **±4 m** on X and Z (8×8 m), centered at the origin. No ground mesh — dark/neutral stage + grid.

## OSC addresses

Listen defaults: **UDP `0.0.0.0:9000`** (Protocol panel).

### `/genesis/room/person` (primary)

Floor person pose for the room effect.

| Args | Meaning |
|------|---------|
| `,ff` `x z` | XZ floor meters, room-centered. Feet height uses settings default (`0.05`). |
| `,fff` `x y z` | Full position; `y` is feet height. |
| `,ffff` `x y z count` | As above; `count` scales feet/ray/wall bursts. |
| `,fff` `x z count` | If the third value is a large integer-ish number, it is treated as burst scale (same heuristic as `/genesis/particles/emit`). |

**Behavior on each message (deterministic with shared `scene_seed`):**

1. Move **Person Feet** emitter to the person (continuous rate + optional burst).
2. Cast a ray from **room center** through the person to the XZ wall AABB.
3. Burst soft particles along that ray (**Center Ray**).
4. Move **Wall Trace** to the wall hit and burst there.

### `/genesis/particles/emit` (still supported)

Generic XY tracker emit (see Protocol panel). On the room example it defaults to the Person Feet path so legacy streams still move feet without the wall/ray path.

## Protocol settings

`OscRoomPersonSettings` (Protocol UI → **Room person**):

- `room_half_x` / `room_half_z` — AABB half-extents (meters)
- `feet_burst` / `wall_burst` / `ray_samples` / `ray_burst_each`
- toggles: track feet/wall, burst feet/ray/wall

## Renderbridge notes

- Same OSC stream + same scene + same `scene_seed` → matching particle sequences on dual GPU.
- Room person events are queued with sequences (like particle emit) so each GPU session can apply them via its own cursor.
- Prefer `/genesis/room/person` at a modest rate (e.g. 10–30 Hz) for smooth feet tracking without flooding bursts.
