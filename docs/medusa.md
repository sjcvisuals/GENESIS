# Medusa (lean room OSC example)

**File → Examples → Medusa**

Single tracked person, dual ambient rings, one wall plane with line-trace debug. No 10× person pool baked into the scene.

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

Feet / Wall emitters appear under `Scene/Person Pool` only when `/genesis/room/person` arrives (`max_persons = 1`).

## Protocol (auto on load)

- Listen UDP `0.0.0.0:9000`
- `/genesis/room/person` `,iff` id x z (preferred)
- `wall_ray_mode` = Prefer plane → distance
- `debug_draw_wall_ray` = on (editor overlay; off under Renderbridge)
- Radii: outer 3 m, tank 1 m

Same protocol details as [room-osc-particles.md](room-osc-particles.md).
