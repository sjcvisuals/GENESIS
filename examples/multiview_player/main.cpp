// Player-side multi-view harness: one scene, N topology outputs, shared production clock.
// Simulates how PIXERA / production playback may reference Genesis across large canvases.
// The editor stays single-monitor and does not use this path.
// Uses windowed HostBridge (sequential per-output renders at capped presentation size).

#include "host/MultiViewHostSession.hpp"
#include "runtime/ProductionTopology.hpp"
#include "runtime/RuntimePaths.hpp"
#include "scene/ScenePath.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path moduleDirectory() {
    if (const char* base = SDL_GetBasePath()) {
        // SDL3: GetBasePath is owned by SDL — do not SDL_free.
        return std::filesystem::path(base);
    }
    return {};
}

void trySetSceneOutputCamera(genesis::MultiViewHostSession& session) {
    for (const genesis::Node& node : session.bridge().sceneRuntime().scene().nodes) {
        if (!node.camera.has_value()) {
            continue;
        }
        session.bridge().sceneRuntime().scene().setActiveOutputCamera(node.id);
        return;
    }
}

void printUsage() {
    std::cout << "Genesis Multi-View Player — playback/topology simulation (not the editor)\n"
              << "Usage: genesis_multiview_player <topology.json> [scene_resource] "
                 "[--machine ID] [--gpu ID] [--frames N]\n"
              << "  Defaults: scene=demo, machine=machine-a, gpu=machine-a-gpu-0, frames=7\n"
              << "  Editor (genesis_gltf_viewer) remains single-monitor authoring only.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            printUsage();
            return argc < 2 ? 1 : 0;
        }

        const std::filesystem::path module_dir = moduleDirectory();
        genesis::setRuntimeAssetRoots(module_dir);

        const std::filesystem::path topology_path = argv[1];
        std::string scene_resource = "demo";
        std::string machine_id = "machine-a";
        std::string gpu_id = "machine-a-gpu-0";
        int total_frames = 7;

        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--machine" && i + 1 < argc) {
                machine_id = argv[++i];
            } else if (arg == "--gpu" && i + 1 < argc) {
                gpu_id = argv[++i];
            } else if (arg == "--frames" && i + 1 < argc) {
                total_frames = std::stoi(argv[++i]);
            } else if (arg.rfind("--", 0) != 0) {
                scene_resource = arg;
            }
        }

        const genesis::ProductionTopology topology = genesis::loadProductionTopology(topology_path);
        const genesis::TopologyValidationResult validation = genesis::validateProductionTopology(topology);
        if (!validation.ok()) {
            spdlog::error("Topology validation failed");
            for (const genesis::TopologyValidationIssue& error : validation.errors) {
                spdlog::error("  {}", error.message);
            }
            return 1;
        }

        genesis::MultiViewHostConfig config{};
        config.node_assignment.machine_id = machine_id;
        config.node_assignment.gpu_id = gpu_id;
        config.window_title = "GENESIS Multi-View";

        genesis::MultiViewHostSession session(topology, config);

        const std::optional<genesis::RuntimeSceneResource> resolved =
            genesis::resolveRuntimeSceneResource(module_dir, scene_resource);
        if (!resolved.has_value()) {
            spdlog::error("Scene resource not found: {}", scene_resource);
            return 1;
        }

        if (!session.tryLoadResolved(*resolved)) {
            spdlog::error("Failed to load scene '{}': {}", scene_resource, session.lastLoadError());
            return 1;
        }

        trySetSceneOutputCamera(session);
        spdlog::info("Multi-view session: {} outputs, scene '{}'", session.outputAssignments().size(),
                     scene_resource);

        double production_time_seconds = 0.0;
        std::uint64_t production_frame_number = 0;
        const double frame_step = (topology.logical_canvas.frame_rate_hz > 0.0)
                                      ? 1.0 / topology.logical_canvas.frame_rate_hz
                                      : 1.0 / 50.0;

        const int warmup_frames = std::min(2, std::max(0, total_frames - 1));
        const int measure_frames = std::max(1, total_frames - warmup_frames);

        genesis::MultiViewFrameMetrics accumulated{};

        for (int frame_index = 0; frame_index < total_frames; ++frame_index) {
            session.bridge().pollEvents();
            if (session.bridge().shouldClose()) {
                break;
            }

            genesis::HostTimingInput timing{};
            timing.production_time_seconds = production_time_seconds;
            timing.production_frame_number = production_frame_number;
            if (frame_index == 0) {
                timing.force_discontinuity = true;
            }

            session.advanceProductionTime(timing);

            const genesis::MultiViewFrameMetrics frame_metrics = session.renderAllOutputs(true);

            if (frame_index >= warmup_frames) {
                accumulated.shared_scene_update_ms += frame_metrics.shared_scene_update_ms;
                accumulated.total_output_render_ms += frame_metrics.total_output_render_ms;
                accumulated.wall_frame_ms += frame_metrics.wall_frame_ms;
                accumulated.output_count = frame_metrics.output_count;

                if (accumulated.outputs.size() != frame_metrics.outputs.size()) {
                    accumulated.outputs = frame_metrics.outputs;
                    for (genesis::OutputRenderMetrics& output : accumulated.outputs) {
                        output.render_ms = 0.0;
                    }
                }

                for (std::size_t i = 0; i < frame_metrics.outputs.size(); ++i) {
                    accumulated.outputs[i].render_ms += frame_metrics.outputs[i].render_ms;
                    accumulated.outputs[i].draw_calls = frame_metrics.outputs[i].draw_calls;
                    accumulated.outputs[i].triangles = frame_metrics.outputs[i].triangles;
                    accumulated.outputs[i].visible_mesh_nodes =
                        frame_metrics.outputs[i].visible_mesh_nodes;
                }
            }

            production_time_seconds += frame_step;
            ++production_frame_number;
        }

        const double measure_count = static_cast<double>(measure_frames);
        std::cout << "Multi-view render metrics (avg over " << measure_frames << " frames @ "
                  << topology.logical_canvas.frame_rate_hz << " Hz)\n";
        std::cout << "  Node: " << machine_id << " / " << gpu_id << '\n';
        std::cout << "  Outputs: " << accumulated.output_count << '\n';
        std::cout << "  Shared scene update: " << (accumulated.shared_scene_update_ms / measure_count)
                  << " ms\n";
        std::cout << "  Total output render: " << (accumulated.total_output_render_ms / measure_count)
                  << " ms\n";
        std::cout << "  Wall frame: " << (accumulated.wall_frame_ms / measure_count) << " ms\n\n";

        for (const genesis::OutputRenderMetrics& output : accumulated.outputs) {
            std::cout << "  " << output.output_id << " (" << output.width << "x" << output.height << ")\n";
            std::cout << "    render: " << (output.render_ms / measure_count) << " ms\n";
            std::cout << "    draw calls: " << output.draw_calls << ", triangles: " << output.triangles
                      << ", visible meshes: " << output.visible_mesh_nodes << '\n';
        }
        std::cout.flush();

        genesis::clearRuntimeAssetRoots();
        return 0;
    } catch (const std::exception& ex) {
        spdlog::critical("Multi-view player fatal error: {}", ex.what());
        return 1;
    }
}
