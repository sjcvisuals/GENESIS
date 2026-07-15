# GENESIS Architecture

GENESIS is a lean Vulkan forward renderer for realtime 3D graphics in live production contexts.

## Systems

| Directory   | Purpose                                      |
|-------------|----------------------------------------------|
| `engine/`   | Application loop orchestration                 |
| `platform/` | SDL3 window, input, timer, ImGui integration |
| `renderer/` | Vulkan device, swapchain, draw submission    |
| `scene/`    | Scene graph (upcoming)                       |
| `assets/`   | glTF and texture loading (upcoming)          |
| `materials/`| PBR material definitions (upcoming)         |
| `shaders/`  | GLSL sources compiled to SPIR-V              |
| `examples/` | Milestone demo applications                  |

## Frame flow

1. Poll SDL events (forwarded to ImGui and input)
2. Update scene / camera
3. Acquire swapchain image, record command buffer (handles resize)
4. ImGui `NewFrame` + debug UI (after swapchain recreation)
5. Render scene passes, then ImGui
6. Present

## Design constraints

- No ECS, gameplay, networking, or scripting in the core renderer
- Forward PBR rendering path
- Scene graph aligned with glTF node hierarchies
- Performance and simplicity over feature count
