#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace genesis {

struct FrameStats;
class ParticleWorld;

// Shared performance snapshot for editor overlay + Pixera Renderbridge HUD.
struct PerfDebugSnapshot {
    float fps = 0.0f;
    float frame_ms = 0.0f;
    float update_ms = 0.0f;
    float imgui_ms = 0.0f;
    float draw_ms = 0.0f;

    std::uint32_t draw_calls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t visible_mesh_nodes = 0;
    std::uint32_t scene_nodes = 0;
    std::uint32_t active_lights = 0;

    std::uint32_t particles_alive = 0;
    std::uint32_t particle_emitters = 0;
    std::uint32_t particle_steps = 0;
    bool particle_gpu = false;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    char gpu_name[96]{};
};

void fillPerfDebugFromFrameStats(PerfDebugSnapshot& snap, const FrameStats& stats);
void fillPerfDebugParticles(PerfDebugSnapshot& snap, const ParticleWorld& world);

// Multi-line HUD text (ASCII). Returns bytes written (excluding NUL), capped at out_size-1.
inline std::size_t formatPerfDebugText(char* out, std::size_t out_size, const PerfDebugSnapshot& s) {
    if (!out || out_size == 0) {
        return 0;
    }
    const int n = std::snprintf(
        out, out_size,
        "GENESIS PERF\n"
        "FPS  %.1f   frame %.2f ms\n"
        "  update %.2f  ui %.2f  draw %.2f\n"
        "Draw %u   tris %u\n"
        "Meshes %u / nodes %u   lights %u\n"
        "Particles %u alive  %u emitters\n"
        "  steps %u  sim %s\n"
        "GPU %s\n"
        "%ux%u",
        static_cast<double>(s.fps), static_cast<double>(s.frame_ms),
        static_cast<double>(s.update_ms), static_cast<double>(s.imgui_ms),
        static_cast<double>(s.draw_ms), s.draw_calls, s.triangles, s.visible_mesh_nodes,
        s.scene_nodes, s.active_lights, s.particles_alive, s.particle_emitters, s.particle_steps,
        s.particle_gpu ? "GPU" : "CPU", s.gpu_name[0] ? s.gpu_name : "(unknown)", s.width,
        s.height);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n) < out_size ? static_cast<std::size_t>(n) : out_size - 1;
}

// Lightweight wall-clock timer for Renderbridge / headless paths (no SDL).
class PerfFrameClock {
public:
    void beginFrame() {
        frame_start_ = Clock::now();
        has_update_end_ = false;
        has_draw_end_ = false;
    }
    void markUpdateEnd() {
        update_end_ = Clock::now();
        has_update_end_ = true;
    }
    void markDrawEnd() {
        draw_end_ = Clock::now();
        has_draw_end_ = true;
    }

    void endFrame(PerfDebugSnapshot& snap) {
        const auto now = Clock::now();
        const Clock::time_point draw_stop = has_draw_end_ ? draw_end_ : now;
        const Clock::time_point update_stop = has_update_end_ ? update_end_ : draw_stop;
        const float frame_ms = msBetween(frame_start_, now);
        snap.frame_ms = frame_ms;
        snap.update_ms = msBetween(frame_start_, update_stop);
        snap.draw_ms = msBetween(update_stop, draw_stop);
        snap.imgui_ms = 0.0f;
        if (frame_ms > 0.01f) {
            const float instant_fps = 1000.0f / frame_ms;
            snap.fps = snap.fps > 1.0f ? (snap.fps * 0.9f + instant_fps * 0.1f) : instant_fps;
        }
        has_update_end_ = false;
        has_draw_end_ = false;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point frame_start_{};
    Clock::time_point update_end_{};
    Clock::time_point draw_end_{};
    bool has_update_end_ = false;
    bool has_draw_end_ = false;

    static float msBetween(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    }
};

} // namespace genesis
