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

bool tryParseOscTuio2Dcur(const OscMessage& message, const OscRoomPersonSettings& settings,
                          OscTuio2DcurParse& out) {
    if (!settings.tuio_enabled || message.address != settings.tuio_address) {
        return false;
    }
    if (message.args.empty() || message.args[0].type != OscArgType::String) {
        return false;
    }

    out = OscTuio2DcurParse{};
    const std::string& cmd = message.args[0].string;
    if (cmd == "set") {
        // ,sifffff → [set, id, x, y, Xvel, Yvel, m] — need at least id + x + y
        if (message.args.size() < 4) {
            return false;
        }
        out.command = OscTuio2DcurCommand::Set;
        out.set.session_id = message.args[1].type == OscArgType::Int32
                                 ? message.args[1].i32
                                 : static_cast<std::int32_t>(std::lround(oscArgAsFloat(message.args[1])));
        out.set.x = oscArgAsFloat(message.args[2]);
        out.set.y = oscArgAsFloat(message.args[3]);
        return true;
    }
    if (cmd == "alive") {
        // ,siiii… → [alive, id0, id1, …] — ints ARE session IDs (not a count).
        out.command = OscTuio2DcurCommand::Alive;
        out.alive.session_ids.reserve(message.args.size() > 0 ? message.args.size() - 1 : 0);
        for (std::size_t i = 1; i < message.args.size(); ++i) {
            const OscArg& arg = message.args[i];
            out.alive.session_ids.push_back(
                arg.type == OscArgType::Int32
                    ? arg.i32
                    : static_cast<std::int32_t>(std::lround(oscArgAsFloat(arg))));
        }
        return true;
    }
    if (cmd == "fseq") {
        if (message.args.size() < 2) {
            return false;
        }
        out.command = OscTuio2DcurCommand::Fseq;
        out.fseq.frame = message.args[1].type == OscArgType::Int32
                             ? message.args[1].i32
                             : static_cast<std::int32_t>(std::lround(oscArgAsFloat(message.args[1])));
        return true;
    }
    return false;
}

void mapTuioSetToRoomPersonEvent(const OscRoomPersonSettings& settings, const OscTuio2DcurSet& set,
                                 OscRoomPersonEvent& out_event) {
    out_event = OscRoomPersonEvent{};
    out_event.person_id = set.session_id;
    // Horizontal floor plane (same as room feet): TUIO x→scene X, TUIO y→scene Z.
    out_event.x = set.x * settings.tuio_scale + settings.tuio_offset.x + settings.room_center.x;
    out_event.z = set.y * settings.tuio_scale + settings.tuio_offset.y + settings.room_center.z;
    out_event.y = settings.feet_height;
    out_event.has_y = true;
    out_event.count = 0;
    out_event.from_tuio = true;
}

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

bool intersectRayPlane(const glm::vec3& origin, const glm::vec3& dir, const WallPlane& plane,
                       float max_t, glm::vec3& out_hit, float* out_t) {
    const float denom = glm::dot(plane.normal, dir);
    if (std::fabs(denom) < 1e-6f) {
        return false;
    }
    const float t = glm::dot(plane.point - origin, plane.normal) / denom;
    if (t < 1e-4f || t > max_t) {
        return false;
    }
    const glm::vec3 hit = origin + dir * t;
    // Finite panel: reject hits outside the Plane actor's rectangle.
    const glm::vec3 rel = hit - plane.point;
    const float u = glm::dot(rel, plane.tangent);
    const float v = glm::dot(rel, plane.bitangent);
    if (std::fabs(u) > plane.half_u + 1e-4f || std::fabs(v) > plane.half_v + 1e-4f) {
        return false;
    }
    out_hit = hit;
    if (out_t) {
        *out_t = t;
    }
    return true;
}

namespace {

OscRoomPersonPose poseFixedDistance(const OscRoomPersonSettings& settings,
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
    pose.used_plane_hit = false;
    return pose;
}

} // namespace

OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event) {
    return oscRoomPersonPose(settings, event, nullptr);
}

OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event,
                                    const WallPlane* wall_plane) {
    OscRoomPersonPose pose = poseFixedDistance(settings, event);
    const bool try_plane =
        wall_plane != nullptr &&
        (settings.wall_ray_mode == WallRaySpawnMode::PlaneCollision ||
         settings.wall_ray_mode == WallRaySpawnMode::PreferPlaneThenDistance);
    if (!try_plane) {
        return pose;
    }

    // Horizontal center→person ray at wall spawn height (matches cylinder install framing).
    const glm::vec3 origin(settings.room_center.x, settings.wall_spawn_height,
                           settings.room_center.z);
    glm::vec3 dir(pose.feet.x - origin.x, 0.0f, pose.feet.z - origin.z);
    const float len = glm::length(dir);
    if (len < 1e-4f) {
        dir = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        dir /= len;
    }

    const float max_t = std::max(0.5f, settings.wall_ray_max_distance);
    glm::vec3 hit{};
    if (intersectRayPlane(origin, dir, *wall_plane, max_t, hit)) {
        pose.wall = hit;
        pose.used_plane_hit = true;
    }
    // Miss → keep fixed-distance wall (PlaneCollision and PreferPlane share this fallback).
    return pose;
}

} // namespace genesis
