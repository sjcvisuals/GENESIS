#include "engine/Application.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

namespace genesis {

Application::Application(const ApplicationConfig& config) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("GENESIS starting");

    WindowConfig window_config{};
    window_config.title = config.title;
    window_config.width = config.width;
    window_config.height = config.height;
    window_ = std::make_unique<Window>(window_config);
    renderer_ = std::make_unique<Renderer>(*window_);
    renderer_->enableImGuiLayoutPersistence();
}

Application::~Application() = default;

void Application::pollEvents() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        input_.onEvent(event);
        processWindowEvent(*window_, event);
    }
}

int Application::run() {
    try {
        const double perf_freq = static_cast<double>(SDL_GetPerformanceFrequency());

        while (!window_->shouldClose()) {
            const uint64_t frame_start = SDL_GetPerformanceCounter();

            timer_.tick();
            input_.beginFrame();
            pollEvents();

            uint64_t phase_start = SDL_GetPerformanceCounter();
            onUpdate(timer_.deltaTime());
            const uint64_t update_end = SDL_GetPerformanceCounter();

            renderer_->beginFrame();

            renderer_->beginUiFrame();
            onImGui();
            onRender();
            const uint64_t imgui_end = SDL_GetPerformanceCounter();

            onDraw();
            const uint64_t draw_end = SDL_GetPerformanceCounter();

            renderer_->endFrame();

            input_.resetMouseDelta();

            frame_stats_.frame_ms = timer_.deltaTime() * 1000.0f;
            frame_stats_.update_ms =
                static_cast<float>(static_cast<double>(update_end - phase_start) * 1000.0 / perf_freq);
            frame_stats_.imgui_ms =
                static_cast<float>(static_cast<double>(imgui_end - update_end) * 1000.0 / perf_freq);
            frame_stats_.draw_ms =
                static_cast<float>(static_cast<double>(draw_end - imgui_end) * 1000.0 / perf_freq);
            (void)frame_start;
        }
    } catch (const std::exception& ex) {
        spdlog::critical("Runtime error: {}", ex.what());
        return 1;
    }

    spdlog::info("GENESIS shutting down");
    return 0;
}

void Application::onImGui() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 140.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("GENESIS", nullptr, flags);
    ImGui::Text("FPS: %.1f", timer_.fps());
    ImGui::Text("Frame: %.3f ms", frame_stats_.frame_ms);
    ImGui::Text("  Update: %.3f ms", frame_stats_.update_ms);
    ImGui::Text("  UI: %.3f ms", frame_stats_.imgui_ms);
    ImGui::Text("  Draw: %.3f ms", frame_stats_.draw_ms);

    if (show_render_stats_) {
        ImGui::Separator();
        ImGui::Text("Draw calls: %u", frame_stats_.draw_calls);
        ImGui::Text("Triangles: %u", frame_stats_.triangles);
        ImGui::Text("Visible meshes: %u", frame_stats_.visible_mesh_nodes);
        ImGui::Text("Scene nodes: %u", frame_stats_.scene_nodes);
        ImGui::Text("Active lights: %u", frame_stats_.active_lights);
    }

    ImGui::Text("GPU: %s", renderer_->deviceName());
    ImGui::Text("Resolution: %d x %d", window_->width(), window_->height());
    ImGui::End();
}

void Application::onDraw() {
    renderer_->drawTriangle();
}

} // namespace genesis
