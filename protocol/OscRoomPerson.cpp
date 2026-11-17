#include "protocol/OscRoomPerson.hpp"

#include "protocol/OscParser.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace genesis {
namespace {

bool isIntegerish(float v) {
    return std::fabs(v - std::round(v)) < 1e-3f;
}

} // namespace

bool tryParseOscRoomPerson(const OscMessage& message, const OscRoomPersonSettings& settings,
                           OscRoomPersonEvent& out_event) {
    if (!settings.enabled || message.address != settings.address) {
        return false;
    }
    if (message.args.size() < 2) {
        return false;
    }

    out_event = OscRoomPersonEvent{};
    out_event.person_id = 0;
    out_event.count = 0;
    out_event.has_y = false;

    std::size_t i = 0;
    // Preferred: leading Int32 person id (`,iff` / `,ifff` / `,iffff`).
    // Float-only messages keep legacy x/z parsing with person_id = 0 so `,ff` / `,fff` demos work.
    if (message.args[0].type == OscArgType::Int32) {
        out_event.person_id = message.args[0].i32;
        i = 1;
        if (message.args.size() - i < 2) {
            return false;
        }
    }

    const std::size_t remain = message.args.size() - i;
    if (remain == 2) {
        out_event.x = oscArgAsFloat(message.args[i + 0]);
        out_event.z = oscArgAsFloat(message.args[i + 1]);
    } else if (remain == 3) {
        out_event.x = oscArgAsFloat(message.args[i + 0]);
        const float third = oscArgAsFloat(message.args[i + 2]);
        const float mid = oscArgAsFloat(message.args[i + 1]);
        // id + x y z  OR  x y z  OR  x z count
        // When mid looks like height (small) and third is free → x y z.
        // When third is large integer-ish → x z count (legacy).
        if (std::fabs(third) >= 1.0f && isIntegerish(third) && std::fabs(mid) > 0.5f) {
            out_event.z = mid;
            out_event.count = static_cast<std::uint32_t>(std::max(0.0f, std::round(third)));
        } else if (std::fabs(mid) <= 2.5f && !isIntegerish(mid)) {
            // x y z (y = feet height)
            out_event.y = mid;
            out_event.has_y = true;
            out_event.z = third;
        } else if (std::fabs(third) >= 1.0f && isIntegerish(third)) {
            out_event.z = mid;
            out_event.count = static_cast<std::uint32_t>(std::max(0.0f, std::round(third)));
        } else {
            out_event.y = mid;
            out_event.has_y = true;
            out_event.z = third;
        }
    } else {
        // x y z [count]
        out_event.x = oscArgAsFloat(message.args[i + 0]);
        out_event.y = oscArgAsFloat(message.args[i + 1]);
        out_event.has_y = true;
        out_event.z = oscArgAsFloat(message.args[i + 2]);
        if (remain >= 4) {
            out_event.count = static_cast<std::uint32_t>(
                std::max(0.0f, std::round(oscArgAsFloat(message.args[i + 3]))));
        }
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

    float radius = settings.room_radius;
    if (radius < 0.1f) {
        radius = std::max(0.1f, std::min(settings.room_half_x, settings.room_half_z));
    }
    pose.wall = glm::vec3(c.x + d.x * radius, settings.wall_spawn_height, c.y + d.y * radius);
    return pose;
}

} // namespace genesis
