// CPU-only determinism harness: two independent ProductionClock + Scene instances,
// identical timing replay, compare scene-state hashes per frame.

#include "runtime/DeterministicAnimation.hpp"
#include "runtime/GenesisFrameTime.hpp"
#include "runtime/SceneStateHash.hpp"
#include "scene/Scene.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TimingReplayFrame {
    double production_time_seconds = 0.0;
    std::optional<std::uint64_t> production_frame_number;
    bool is_paused = false;
    bool force_discontinuity = false;
};

struct TimingReplay {
    genesis::ProductionClockConfig clock_config{};
    std::vector<TimingReplayFrame> frames;
};

TimingReplay loadTimingReplay(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open timing replay: " + path.string());
    }

    nlohmann::json root;
    in >> root;
    if (root.value("format", std::string{}) != "genesis-timing-replay") {
        throw std::runtime_error("Not a genesis-timing-replay document");
    }

    TimingReplay replay{};
    replay.clock_config.output_frame_rate_hz = root.value("output_frame_rate_hz", 50.0);
    replay.clock_config.simulation_step_seconds = root.value("simulation_step_seconds", 0.01);
    replay.clock_config.scene_seed = root.value("scene_seed", 1u);

    for (const nlohmann::json& frame_json : root.at("frames")) {
        TimingReplayFrame frame{};
        frame.production_time_seconds = frame_json.at("production_time_seconds").get<double>();
        if (frame_json.contains("production_frame_number") &&
            !frame_json["production_frame_number"].is_null()) {
            frame.production_frame_number =
                frame_json["production_frame_number"].get<std::uint64_t>();
        }
        frame.is_paused = frame_json.value("is_paused", false);
        frame.force_discontinuity = frame_json.value("force_discontinuity", false);
        replay.frames.push_back(frame);
    }
    return replay;
}

genesis::Scene makeDemoScene() {
    genesis::Scene scene{};
    const genesis::NodeId root = scene.createEmptyNode("Root");
    const genesis::NodeId target = scene.createMeshNode("AnimatedTarget", 0, root);
    if (genesis::Node* node = scene.find(target)) {
        node->local.position = {0.0f, 0.5f, 0.0f};
    }
    return scene;
}

genesis::HostTimingInput toHostInput(const TimingReplayFrame& frame) {
    genesis::HostTimingInput input{};
    input.production_time_seconds = frame.production_time_seconds;
    input.production_frame_number = frame.production_frame_number;
    input.is_paused = frame.is_paused;
    input.force_discontinuity = frame.force_discontinuity;
    return input;
}

void runReplayInstance(const TimingReplay& replay, std::vector<std::string>& out_hashes) {
    genesis::ProductionClock clock(replay.clock_config);
    genesis::Scene scene = makeDemoScene();
    genesis::DeterministicAnimationConfig anim_config{};
    anim_config.node_path = "Root/AnimatedTarget";
    anim_config.rotation_radians_per_second = 1.25f;
    anim_config.position_base = {0.0f, 0.5f, 0.0f};
    anim_config.position_amplitude = 0.15f;

    out_hashes.clear();
    out_hashes.reserve(replay.frames.size());

    for (const TimingReplayFrame& frame : replay.frames) {
        const genesis::GenesisFrameTime frame_time = clock.advance(toHostInput(frame));
        genesis::applyDeterministicAnimation(scene, frame_time, anim_config);
        scene.updateWorldTransforms();
        out_hashes.push_back(genesis::sceneStateContentHash(scene));
    }
}

void printUsage() {
    std::cout << "Usage: genesis_determinism_harness <timing-replay.json>\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    try {
        const TimingReplay replay = loadTimingReplay(argv[1]);

        std::vector<std::string> hashes_a;
        std::vector<std::string> hashes_b;
        runReplayInstance(replay, hashes_a);
        runReplayInstance(replay, hashes_b);

        if (hashes_a.size() != hashes_b.size()) {
            std::cerr << "Hash count mismatch\n";
            return 1;
        }

        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < hashes_a.size(); ++i) {
            const TimingReplayFrame& frame = replay.frames[i];
            if (hashes_a[i] != hashes_b[i]) {
                ++mismatches;
                std::cout << "frame " << i << " time=" << frame.production_time_seconds
                          << " hash_a=" << hashes_a[i] << " hash_b=" << hashes_b[i] << " MISMATCH\n";
            } else {
                std::cout << "frame " << i << " time=" << frame.production_time_seconds
                          << " hash=" << hashes_a[i] << " ok\n";
            }
        }

        std::cout << "\nframes: " << hashes_a.size() << ", mismatches: " << mismatches << '\n';
        return mismatches == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "determinism harness failed: " << ex.what() << '\n';
        return 1;
    }
}
