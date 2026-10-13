#include "protocol/OscParticleEmit.hpp"

#include "protocol/OscParser.hpp"

#include <algorithm>
#include <cmath>

namespace genesis {

bool tryParseOscParticleEmit(const OscMessage& message, const OscParticleEmitSettings& settings,
                             OscParticleEmitEvent& out_event) {
    if (!settings.enabled || message.address != settings.address) {
        return false;
    }
    if (message.args.size() < 2) {
        return false;
    }

    out_event = OscParticleEmitEvent{};
    out_event.x = oscArgAsFloat(message.args[0]);
    out_event.y = oscArgAsFloat(message.args[1]);
    out_event.count = settings.default_count;
    out_event.has_z = false;

    if (message.args.size() == 3) {
        const float third = oscArgAsFloat(message.args[2]);
        // Heuristic: large / integer-ish values are particle counts; small floats are Z.
        if (std::fabs(third) >= 1.0f && std::fabs(third - std::round(third)) < 1e-3f) {
            out_event.count = static_cast<std::uint32_t>(std::max(0.0f, std::round(third)));
        } else {
            out_event.z = third;
            out_event.has_z = true;
        }
    } else if (message.args.size() >= 4) {
        out_event.z = oscArgAsFloat(message.args[2]);
        out_event.has_z = true;
        out_event.count =
            static_cast<std::uint32_t>(std::max(0.0f, std::round(oscArgAsFloat(message.args[3]))));
    }

    out_event.count = std::clamp(out_event.count, 0u, settings.max_count);
    return out_event.count > 0;
}

} // namespace genesis
