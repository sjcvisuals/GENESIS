#pragma once

#include "WinPrefix.h"

#include <cstdint>
#include <string>

namespace rxext::genesis_remover {

// Best-effort GPU pick when PIXERA leaves Adapter LUID at 0.
// Prefer adapters that have at least one Windows display attached
// (AttachedToDesktop), then NVIDIA / discrete over AMD iGPU.
// This is NOT PIXERA's internal output-assignment table (that is not exposed
// to C++ extensions) - it approximates "GPUs driving monitors".
std::uint64_t pickAdapterLuidWithDisplays();

std::string adapterDescription(std::uint64_t luid);

} // namespace rxext::genesis_remover
