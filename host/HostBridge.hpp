#pragma once

#include "engine/FrameStats.hpp"
#include "host/HostSceneCommand.hpp"
#include "host/HostTypes.hpp"
#include "runtime/GenesisFrameTime.hpp"
#include "runtime/RuntimeScene.hpp"
#include "renderer/FrameUniforms.hpp"
#include "scene/SceneDrawBatch.hpp"
#include "scene/SceneRuntime.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace genesis {

class Renderer;
class Window;

// Host-facing render session. PIXERA RenderBridge and future media-server forks
// both drive scene + frame output through this API; only the host shell differs.
class HostBridge {
public:
    explicit HostBridge(const HostBridgeConfig& config);
    ~HostBridge();

    HostBridge(const HostBridge&) = delete;
    HostBridge& operator=(const HostBridge&) = delete;

    // --- Lifecycle (windowed bootstrap; external-surface bootstrap comes later) ---
    bool shouldClose() const;
    void pollEvents();
    void resize(int width, int height);

    int width() const;
    int height() const;
    const char* deviceName() const;

    // --- Scene I/O ---
    void loadDemo();
    void loadGltf(const std::filesystem::path& path);
    void loadDocument(const std::filesystem::path& path);
    void saveDocument(const std::filesystem::path& path);
    bool tryLoadRuntimeResource(const RuntimeSceneResource& resource, std::string* out_error = nullptr);

    // --- Host scene control ---
    bool applyCommand(const HostSceneCommand& command, std::string* out_error = nullptr);
    void updateTransforms();

    const GenesisFrameTime& currentFrameTime() const { return current_frame_time_; }

    SceneRuntime& sceneRuntime() { return *scene_; }
    const SceneRuntime& sceneRuntime() const { return *scene_; }

    // --- Frame ---
    void beginFrame(const HostFrameInput& input);
    void renderScene();
    void endFrame();

    const FrameStats& frameStats() const { return frame_stats_; }

private:
    void prepareFrameUniforms(const HostFrameInput& input);
    void drawStatsOverlay();

    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<SceneRuntime> scene_;

    std::vector<SceneDrawCommand> draw_commands_;
    std::vector<SceneMeshBatch> draw_batches_;
    FrameUniforms frame_uniforms_{};
    FrameStats frame_stats_{};
    ProductionClock production_clock_{};
    GenesisFrameTime current_frame_time_{};
    bool stats_overlay_ = false;
};

} // namespace genesis
