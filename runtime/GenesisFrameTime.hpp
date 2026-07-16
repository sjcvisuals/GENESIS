#pragma once

#include <cstdint>
#include <optional>

namespace genesis {

struct GenesisFrameTime {
    std::uint64_t production_frame_number = 0;
    double production_time_seconds = 0.0;
    double delta_time_seconds = 0.0;
    double fixed_step_seconds = 0.01;
    std::uint64_t simulation_step_index = 0;
    std::uint64_t simulation_steps_this_frame = 0;
    std::uint64_t scene_seed = 0;
    bool is_discontinuity = false;
    bool is_paused = false;
};

struct ProductionClockConfig {
    double output_frame_rate_hz = 50.0;
    double simulation_step_seconds = 0.01;
    std::uint64_t scene_seed = 1;
    // Prevent spiral-of-death when host timing stalls.
    std::uint64_t max_simulation_steps_per_frame = 8;
    // Treat host time jumps larger than this as a discontinuity (seek/restart).
    double discontinuity_threshold_seconds = 0.1;
};

struct HostTimingInput {
    double production_time_seconds = 0.0;
    std::optional<std::uint64_t> production_frame_number;
    bool is_paused = false;
    bool force_discontinuity = false;
};

// Host-agnostic production clock. Consumes host timing; never reads wall clock.
class ProductionClock {
public:
    explicit ProductionClock(ProductionClockConfig config = {});

    void setConfig(const ProductionClockConfig& config);
    const ProductionClockConfig& config() const { return config_; }

    void reset(const HostTimingInput& input);
    GenesisFrameTime advance(const HostTimingInput& input);

    const GenesisFrameTime& current() const { return current_; }

private:
    void snapSimulationToTime(double production_time_seconds);

    ProductionClockConfig config_;
    GenesisFrameTime current_{};
    double last_production_time_ = 0.0;
    bool initialized_ = false;
};

} // namespace genesis
