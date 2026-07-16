#include "runtime/GenesisFrameTime.hpp"

#include <algorithm>
#include <cmath>

namespace genesis {

namespace {

constexpr double kDuplicateFrameEpsilon = 1e-9;

} // namespace

ProductionClock::ProductionClock(ProductionClockConfig config) : config_(config) {
    current_.fixed_step_seconds = config_.simulation_step_seconds;
    current_.scene_seed = config_.scene_seed;
}

void ProductionClock::setConfig(const ProductionClockConfig& config) {
    config_ = config;
    current_.fixed_step_seconds = config_.simulation_step_seconds;
    current_.scene_seed = config_.scene_seed;
}

void ProductionClock::snapSimulationToTime(const double production_time_seconds) {
    if (config_.simulation_step_seconds <= 0.0) {
        current_.simulation_step_index = 0;
        return;
    }
    const double steps =
        std::max(0.0, production_time_seconds / config_.simulation_step_seconds);
    current_.simulation_step_index = static_cast<std::uint64_t>(std::floor(steps + 1e-9));
}

void ProductionClock::reset(const HostTimingInput& input) {
    initialized_ = false;
    last_production_time_ = input.production_time_seconds;
    current_ = GenesisFrameTime{};
    current_.fixed_step_seconds = config_.simulation_step_seconds;
    current_.scene_seed = config_.scene_seed;
    current_.production_time_seconds = input.production_time_seconds;
    current_.production_frame_number =
        input.production_frame_number.value_or(
            (config_.output_frame_rate_hz > 0.0)
                ? static_cast<std::uint64_t>(std::llround(
                      input.production_time_seconds * config_.output_frame_rate_hz))
                : 0);
    current_.delta_time_seconds = 0.0;
    current_.simulation_steps_this_frame = 0;
    current_.is_discontinuity = true;
    current_.is_paused = input.is_paused;
    snapSimulationToTime(input.production_time_seconds);
    initialized_ = true;
}

GenesisFrameTime ProductionClock::advance(const HostTimingInput& input) {
    if (input.force_discontinuity || !initialized_) {
        reset(input);
        return current_;
    }

    GenesisFrameTime frame{};
    frame.fixed_step_seconds = config_.simulation_step_seconds;
    frame.scene_seed = config_.scene_seed;
    frame.production_time_seconds = input.production_time_seconds;
    frame.is_paused = input.is_paused;
    frame.production_frame_number =
        input.production_frame_number.value_or(
            (config_.output_frame_rate_hz > 0.0)
                ? static_cast<std::uint64_t>(std::llround(
                      input.production_time_seconds * config_.output_frame_rate_hz))
                : current_.production_frame_number);

    const double delta = input.production_time_seconds - last_production_time_;
    frame.delta_time_seconds = (delta > 0.0) ? delta : 0.0;

    const bool backwards = delta < -kDuplicateFrameEpsilon;
    const bool duplicate = std::abs(delta) <= kDuplicateFrameEpsilon;
    const bool large_jump =
        frame.delta_time_seconds > config_.discontinuity_threshold_seconds;

    if (backwards || large_jump) {
        frame.is_discontinuity = true;
        snapSimulationToTime(input.production_time_seconds);
        frame.simulation_step_index = current_.simulation_step_index;
        frame.simulation_steps_this_frame = 0;
    } else if (input.is_paused || duplicate) {
        frame.is_discontinuity = false;
        frame.simulation_step_index = current_.simulation_step_index;
        frame.simulation_steps_this_frame = 0;
    } else {
        frame.is_discontinuity = false;
        frame.simulation_step_index = current_.simulation_step_index;

        if (config_.simulation_step_seconds > 0.0) {
            std::uint64_t steps = static_cast<std::uint64_t>(std::floor(
                frame.delta_time_seconds / config_.simulation_step_seconds + 1e-9));
            steps = std::min(steps, config_.max_simulation_steps_per_frame);
            frame.simulation_steps_this_frame = steps;
            frame.simulation_step_index = current_.simulation_step_index + steps;
        }
    }

    last_production_time_ = input.production_time_seconds;
    current_ = frame;
    return current_;
}

} // namespace genesis
