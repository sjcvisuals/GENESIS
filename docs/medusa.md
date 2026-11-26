# Medusa (room OSC + TUIO example)

**File → Examples → Medusa**

Opens `assets/scenes/medusa.scene.json` and shows **View → Room Tracking** + **Protocol** so the setup is visible — nothing is silently configured from the filename.

Dual ambient rings, one authored Wall Plane (Collision ON, Hidden in Game), multi-person tracking via TUIO `/tuio/2Dcur` (and `/genesis/room/person`). Feet/Wall emitters appear lazily under `Scene/Person Pool` when people are active.

## Scene

| Asset | Path |
|-------|------|
| Package | `assets/scenes/medusa.gns` |
| Document | `assets/scenes/medusa.scene.json` |

### Hierarchy (authored in the file)

```
Scene
├── Particle System
│   ├── Outer Ring          # ~6 m diameter, rise / float up
│   └── Inner Tank Ring     # ~2 m diameter, floor-outward (no rise)
├── Wall Plane              # Hidden in Game; Collision ON; center→person ray collide
└── Camera
```

`room_tracking` in the JSON (also editable in **View → Room Tracking**) enables people / TUIO / prefer-plane wall ray. Feet / Wall emitters appear under `Scene/Person Pool` when a person id is active (`max_persons = 10`). Idle / missing-from-alive slots park at origin (disabled).

## Build the same setup from scratch

1. Add dual ring emitters (or any ambient systems) under a Particle System.
2. Add a **Plane** mesh → name it e.g. `Wall Plane` → enable **Collision**, optionally **Hidden in Game**.
3. **View → Room Tracking**: Enable room / people tracking; set Wall Plane path; Wall ray mode = Prefer plane, else distance; enable TUIO if using TouchDesigner.
4. **View → Protocol**: set bind IP + port, Listen.
5. **File → Save** — `osc` + `room_tracking` persist with the scene for Pixera.

Per-emitter **Line Trace** (Tugela modules) is separate and fully manual — room wall-ray is the people-tracking path above.

## Protocol

- Listen UDP (authored `osc` block / Protocol panel) — e.g. `0.0.0.0:9000`
- **`/tuio/2Dcur`** via the normal OSC listen port (TD sends these as OSC; no native TUIO client). Raw still in OSC Debug Log:
  - `alive` `,siiii…` `[alive, id0, id1, …]` — ints after `alive` **are session IDs** (active pool); not a count
  - `set` `,sifffff` `[set, id, x, y, Xvel, Yvel, m]` — use **id + x,y** only (vel/m ignored)
  - `fseq` `,si` `[fseq, n]` — frame tick (stored for debug)
- `/genesis/room/person` `,iff` id x z still supported

### Coord mapping (TUIO → scene)

TUIO cursor `x,y` are treated as roughly **normalized ~[-1, 1]** and mapped onto the **horizontal floor** (same plane as room feet):

| TUIO | Scene |
|------|--------|
| `x` | `X = x * tuio_scale + offset.x` (default scale = `room_radius` = 3 m) |
| `y` | `Z = y * tuio_scale + offset.y` |
| (height) | `Y = feet_height` |

### Pool behavior

1. `alive` sets the active ID set (pool size = number of IDs listed).
2. `set` assigns/updates the slot for that session ID and moves Feet (+ Wall ray) to the mapped pose.
3. IDs missing from the next `alive` are parked (origin, emitters disabled). Non-TUIO `/genesis/room/person` ids still use `slot_idle_seconds`.

Same room details as [room-osc-particles.md](room-osc-particles.md).
