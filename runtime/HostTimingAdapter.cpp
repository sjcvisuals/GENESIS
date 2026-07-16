#include "runtime/HostTimingAdapter.hpp"

namespace genesis {

GenesisFrameTime adaptHostTiming(ProductionClock& clock, const HostTimingInput& input) {
    return clock.advance(input);
}

HostTimingInput hostTimingFromPixeraTimeline(const double timeline_time_seconds,
                                             const bool is_paused,
                                             const bool force_discontinuity) {
    HostTimingInput input{};
    input.production_time_seconds = timeline_time_seconds;
    input.is_paused = is_paused;
    input.force_discontinuity = force_discontinuity;
    return input;
}

} // namespace genesis
