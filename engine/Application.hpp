#pragma once

#include "engine/FrameStats.hpp"
#include "platform/Input.hpp"
#include "platform/Timer.hpp"
#include "platform/Window.hpp"
#include "renderer/Renderer.hpp"

#include <memory>
#include <string>

namespace genesis {

struct ApplicationConfig {
    std::string title = "GENESIS";
    int width = 1280;
    int height = 720;
    bool start_maximized = false;
    bool start_fullscreen = false;
    // Applied to ImGui fonts at CreateContext (1.0 = 16 px base before DPI).
    float ui_scale = 1.0f;
};

class Application {
public:
    explicit Application(const ApplicationConfig& config);
    virtual ~Application();

    int run();

protected:
    virtual void onUpdate(float delta_time) {}
    virtual void onRender() {}
    virtual void onImGui();
    virtual void onDraw();

    Window& window() { return *window_; }
    Renderer& renderer() { return *renderer_; }
    Input& input() { return input_; }
    Timer& timer() { return timer_; }
    FrameStats& frameStats() { return frame_stats_; }
    const FrameStats& frameStats() const { return frame_stats_; }
    void setRenderStatsEnabled(bool enabled) { show_render_stats_ = enabled; }

private:
    void pollEvents();

    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    Input input_;
    Timer timer_;
    FrameStats frame_stats_{};
    bool show_render_stats_ = false;
};

} // namespace genesis
