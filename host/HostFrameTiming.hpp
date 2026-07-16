#pragma once

#include "host/HostTypes.hpp"
#include "runtime/GenesisFrameTime.hpp"

namespace genesis {

HostTimingInput hostTimingFromFrameInput(const HostFrameInput& frame);
ProductionClockConfig productionClockConfigFromFrameInput(const HostFrameInput& frame);
GenesisFrameTime adaptHostFrameInput(ProductionClock& clock, const HostFrameInput& frame);

} // namespace genesis
