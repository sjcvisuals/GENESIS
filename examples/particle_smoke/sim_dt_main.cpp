#include "platform/ParticleSimDt.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool near_eq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    using genesis::particleSimDt;

    // Cap ON @ 50: near-period frames snap to exact 20 ms → stable 2×10 ms steps.
    if (!near_eq(particleSimDt(0.018f, true, 50.0f), 0.02f) ||
        !near_eq(particleSimDt(0.020f, true, 50.0f), 0.02f) ||
        !near_eq(particleSimDt(0.022f, true, 50.0f), 0.02f)) {
        std::cerr << "expected near-50fps frames to snap to 0.02\n";
        return 1;
    }

    // Real hitch: limited catch-up (2 periods), never the raw spike.
    if (!near_eq(particleSimDt(0.08f, true, 50.0f), 0.04f)) {
        std::cerr << "expected 80ms hitch to clamp to 2 periods (40ms)\n";
        return 1;
    }

    // Hard clamp even with cap off.
    if (!near_eq(particleSimDt(0.25f, false, 50.0f), 0.05f)) {
        std::cerr << "expected hard clamp to 50ms\n";
        return 1;
    }

    // Cap off: pass-through within clamp.
    if (!near_eq(particleSimDt(0.016f, false, 50.0f), 0.016f)) {
        std::cerr << "expected uncapped pass-through\n";
        return 1;
    }

    std::cout << "particleSimDt ok\n";
    return 0;
}
