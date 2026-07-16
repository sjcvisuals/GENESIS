#include "host/HostFrameTiming.hpp"

namespace genesis {

HostTimingInput hostTimingFromFrameInput(const HostFrameInput& frame) {
    HostTimingInput input{};
    input.production_time_seconds = frame.production_time_seconds;
    input.production_frame_number = frame.production_frame_number;
    input.is_paused = frame.production_paused;
    input.force_discontinuity = frame.production_discontinuity;
    return input;
}

ProductionClockConfig productionClockConfigFromFrameInput(const HostFrameInput& frame) {
    ProductionClockConfig config{};
    config.output_frame_rate_hz = frame.output_frame_rate_hz;
    config.simulation_step_seconds = frame.simulation_step_seconds;
    config.scene_seed = frame.scene_seed;
    return config;
}

GenesisFrameTime adaptHostFrameInput(ProductionClock& clock, const HostFrameInput& frame) {
    clock.setConfig(productionClockConfigFromFrameInput(frame));
    return clock.advance(hostTimingFromFrameInput(frame));
}

} // namespace genesis
