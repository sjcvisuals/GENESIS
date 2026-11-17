#include "protocol/OscRoomPerson.hpp"

#include "protocol/OscParser.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace genesis {

bool tryParseOscRoomPerson(const OscMessage& message, const OscRoomPersonSettings& settings,
                           OscRoomPersonEvent& out_event) {
    if (!settings.enabled || message.address != settings.address) {
        return false;
    }
    if (message.args.size() < 2) {
        return false;
    }

    out_event = OscRoomPersonEvent{};
    out_event.x = oscArgAsFloat(message.args[0]);
    out_event.z = oscArgAsFloat(message.args[1]);
    out_event.count = 0;
    out_event.has_y = false;

    if (message.args.size() == 3) {
        const float third = oscArgAsFloat(message.args[2]);
        // Large integer-ish → burst scale; otherwise treat as feet height Y.
        if (std::fabs(third) >= 1.0f && std::fabs(third - std::round(third)) < 1e-3f) {
            out_event.count = static_cast<std::uint32_t>(std::max(0.0f, std::round(third)));
        } else {
            out_event.y = third;
            out_event.has_y = true;
        }
    } else if (message.args.size() >= 4) {
        // x y z [count] — y is height, z is floor depth.
        out_event.y = oscArgAsFloat(message.args[1]);
        out_event.has_y = true;
        out_event.z = oscArgAsFloat(message.args[2]);
        out_event.count =
            static_cast<std::uint32_t>(std::max(0.0f, std::round(oscArgAsFloat(message.args[3]))));
    }

    out_event.count = std::min(out_event.count, settings.max_burst);
    return true;
}

OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event) {
    OscRoomPersonPose pose{};
    pose.center = settings.room_center;
    const float feet_y = event.has_y ? event.y : settings.feet_height;
    pose.feet = glm::vec3(event.x, feet_y, event.z);

    glm::vec2 c(settings.room_center.x, settings.room_center.z);
    glm::vec2 p(event.x, event.z);
    glm::vec2 d = p - c;
    const float len = glm::length(d);
    if (len < 1e-4f) {
        d = glm::vec2(1.0f, 0.0f);
    } else {
        d /= len;
    }

    // Prefer explicit cylindrical radius; fall back to legacy AABB half-extents.
    float radius = settings.room_radius;
    if (radius < 0.1f) {
        radius = std::max(0.1f, std::min(settings.room_half_x, settings.room_half_z));
    }
    pose.wall = glm::vec3(c.x + d.x * radius, settings.wall_spawn_height, c.y + d.y * radius);
    return pose;
}

} // namespace genesis
