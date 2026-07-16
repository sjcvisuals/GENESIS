#pragma once

#include "runtime/GenesisFrameTime.hpp"

namespace genesis {

GenesisFrameTime adaptHostTiming(ProductionClock& clock, const HostTimingInput& input);

// Maps PIXERA timeline seconds into host timing input.
HostTimingInput hostTimingFromPixeraTimeline(double timeline_time_seconds,
                                             bool is_paused = false,
                                             bool force_discontinuity = false);

} // namespace genesis
