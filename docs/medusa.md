# Medusa (room OSC + TUIO example)

**File → Examples → Medusa**

Dual ambient rings, one wall plane with line-trace debug, multi-person tracking via TUIO `/tuio/2Dcur` (and `/genesis/room/person`). No person pool baked into the scene — Feet/Wall emitters appear lazily under `Scene/Person Pool`.

## Scene

| Asset | Path |
|-------|------|
| Package | `assets/scenes/medusa.gns` |
| Document | `assets/scenes/medusa.scene.json` |

### Hierarchy

```
Scene
├── Particle System
│   ├── Outer Ring          # ~6 m diameter, rise / float up
│   └── Inner Tank Ring     # ~2 m diameter, floor-outward (no rise)
├── Wall Plane              # Hidden in Game; center→person ray collide
└── Camera
```

Feet / Wall emitters appear under `Scene/Person Pool` when a person id is active (`max_persons = 10`). Idle / missing-from-alive slots park at origin (disabled).

## Protocol (auto on load)

- Listen UDP `0.0.0.0:9000`
- **`/tuio/2Dcur`** via the normal OSC listen port (TD sends these as OSC; no native TUIO client). Dedicated ingest — Protocol UI stays lean; raw still in debug log:
  - `alive` `,siiii…` `[alive, id0, id1, …]` — ints after `alive` **are session IDs** (active pool); not a count
  - `set` `,sifffff` `[set, id, x, y, Xvel, Yvel, m]` — use **id + x,y** only (vel/m ignored)
  - `fseq` `,si` `[fseq, n]` — frame tick (stored for debug)
- `/genesis/room/person` `,iff` id x z still supported
- `wall_ray_mode` = Prefer plane → distance
- `debug_draw_wall_ray` = on (editor overlay; off under Renderbridge)
- Radii: outer 3 m, tank 1 m

### Coord mapping (TUIO → scene)

TUIO cursor `x,y` are treated as roughly **normalized ~[-1, 1]** and mapped onto the **horizontal floor** (same plane as room feet):

| TUIO | Scene |
|------|--------|
| `x` | `X = x * tuio_scale + offset.x` (default scale = `room_radius` = 3 m) |
| `y` | `Z = y * tuio_scale + offset.y` |
| (height) | `Y = feet_height` |

So systems move with people on the XZ floor plane from the coordinates just received for that session ID.

### Pool behavior

1. `alive` sets the active ID set (pool size = number of IDs listed).
2. `set` assigns/updates the slot for that session ID and moves Feet (+ Wall ray) to the mapped pose.
3. IDs missing from the next `alive` are parked (origin, emitters disabled). Non-TUIO `/genesis/room/person` ids still use `slot_idle_seconds`.

Same room details as [room-osc-particles.md](room-osc-particles.md).
