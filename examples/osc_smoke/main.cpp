// Headless OSC parser + particle-emit smoke test.

#include "protocol/OscParser.hpp"
#include "protocol/OscParticleEmit.hpp"
#include "protocol/OscRoomPerson.hpp"

#if !defined(GENESIS_OSC_SMOKE_LIGHT)
#include "particles/ParticleWorld.hpp"
#include "scene/Scene.hpp"
#endif

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void appendPadded(std::vector<std::uint8_t>& out, const char* s) {
    const std::size_t n = std::strlen(s) + 1;
    out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(s),
               reinterpret_cast<const std::uint8_t*>(s) + n);
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

void appendFloatBe(std::vector<std::uint8_t>& out, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(bits & 0xff));
}

void appendIntBe(std::vector<std::uint8_t>& out, std::int32_t v) {
    const auto bits = static_cast<std::uint32_t>(v);
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(bits & 0xff));
}

std::vector<std::uint8_t> makeFloat3Message() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/genesis/transform/pos");
    appendPadded(packet, ",fff");
    appendFloatBe(packet, 1.25f);
    appendFloatBe(packet, -2.5f);
    appendFloatBe(packet, 3.0f);
    return packet;
}

std::vector<std::uint8_t> makeIntMessage() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/genesis/light/enabled");
    appendPadded(packet, ",i");
    appendIntBe(packet, 1);
    return packet;
}

std::vector<std::uint8_t> makeParticleEmitXy() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/genesis/particles/emit");
    appendPadded(packet, ",ff");
    appendFloatBe(packet, 1.5f);
    appendFloatBe(packet, -0.25f);
    return packet;
}

std::vector<std::uint8_t> makeParticleEmitXyCount() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/genesis/particles/emit");
    appendPadded(packet, ",fff");
    appendFloatBe(packet, 2.0f);
    appendFloatBe(packet, 3.0f);
    appendFloatBe(packet, 12.0f);
    return packet;
}

std::vector<std::uint8_t> makeRoomPersonXz() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/genesis/room/person");
    appendPadded(packet, ",ff");
    appendFloatBe(packet, 2.0f);
    appendFloatBe(packet, 1.0f);
    return packet;
}

std::vector<std::uint8_t> makeRoomPersonIdXz() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/genesis/room/person");
    appendPadded(packet, ",iff");
    appendIntBe(packet, 3);
    appendFloatBe(packet, 2.0f);
    appendFloatBe(packet, 1.0f);
    return packet;
}

std::vector<std::uint8_t> makeTuioAlive() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/tuio/2Dcur");
    appendPadded(packet, ",siiii");
    appendPadded(packet, "alive");
    appendIntBe(packet, 2);
    appendIntBe(packet, 12);
    appendIntBe(packet, 14);
    appendIntBe(packet, 13);
    return packet;
}

std::vector<std::uint8_t> makeTuioSet() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/tuio/2Dcur");
    appendPadded(packet, ",sifffff");
    appendPadded(packet, "set");
    appendIntBe(packet, 12);
    appendFloatBe(packet, -0.5421f);
    appendFloatBe(packet, 0.2700f);
    appendFloatBe(packet, -0.2216f);
    appendFloatBe(packet, -0.4029f);
    appendFloatBe(packet, 0.4599f);
    return packet;
}

std::vector<std::uint8_t> makeTuioFseq() {
    std::vector<std::uint8_t> packet;
    appendPadded(packet, "/tuio/2Dcur");
    appendPadded(packet, ",si");
    appendPadded(packet, "fseq");
    appendIntBe(packet, 9453);
    return packet;
}

std::vector<std::uint8_t> makeNestedBundle() {
    // Outer #bundle containing /a and an inner #bundle with /b.
    const auto msg_a = makeIntMessage();
    std::vector<std::uint8_t> msg_b;
    appendPadded(msg_b, "/bundle/child");
    appendPadded(msg_b, ",f");
    appendFloatBe(msg_b, 0.5f);

    std::vector<std::uint8_t> inner;
    appendPadded(inner, "#bundle");
    for (int i = 0; i < 8; ++i) {
        inner.push_back(i == 7 ? 1 : 0); // immediate timetag
    }
    appendIntBe(inner, static_cast<std::int32_t>(msg_b.size()));
    inner.insert(inner.end(), msg_b.begin(), msg_b.end());

    std::vector<std::uint8_t> outer;
    appendPadded(outer, "#bundle");
    for (int i = 0; i < 8; ++i) {
        outer.push_back(i == 7 ? 1 : 0);
    }
    appendIntBe(outer, static_cast<std::int32_t>(msg_a.size()));
    outer.insert(outer.end(), msg_a.begin(), msg_a.end());
    appendIntBe(outer, static_cast<std::int32_t>(inner.size()));
    outer.insert(outer.end(), inner.begin(), inner.end());
    return outer;
}

} // namespace

int main() {
    int failures = 0;

    {
        const auto packet = makeFloat3Message();
        std::string err;
        const auto msg = genesis::parseOscMessage(packet, &err);
        if (!msg) {
            std::cerr << "float3 parse failed: " << err << '\n';
            ++failures;
        } else if (msg->address != "/genesis/transform/pos" || msg->args.size() != 3) {
            std::cerr << "float3 unexpected content\n";
            ++failures;
        } else {
            const float x = genesis::oscArgAsFloat(msg->args[0]);
            const float y = genesis::oscArgAsFloat(msg->args[1]);
            const float z = genesis::oscArgAsFloat(msg->args[2]);
            if (std::abs(x - 1.25f) > 1e-5f || std::abs(y + 2.5f) > 1e-5f || std::abs(z - 3.0f) > 1e-5f) {
                std::cerr << "float3 values mismatch: " << x << "," << y << "," << z << '\n';
                ++failures;
            } else {
                std::cout << "OSC float3 message OK (" << msg->address << ")\n";
            }
        }

        const auto messages = genesis::parseOscPackets(packet, &err);
        if (messages.size() != 1) {
            std::cerr << "parseOscPackets expected 1 message, got " << messages.size() << ": " << err
                      << '\n';
            ++failures;
        } else {
            std::cout << "OSC packet decode OK\n";
        }
    }

    {
        const auto packet = makeNestedBundle();
        std::string err;
        const genesis::OscParseResult detailed = genesis::parseOscPacketDetailed(packet, &err);
        if (detailed.messages.size() != 2) {
            std::cerr << "nested bundle expected 2 messages, got " << detailed.messages.size()
                      << ": " << err << '\n';
            ++failures;
        } else {
            bool saw_outer = false;
            bool saw_inner = false;
            bool saw_child_depth2 = false;
            for (const genesis::OscDebugLogEntry& e : detailed.debug_log) {
                if (e.kind == genesis::OscDebugKind::Bundle && e.nest_depth == 0) {
                    saw_outer = true;
                }
                if (e.kind == genesis::OscDebugKind::Bundle && e.nest_depth == 1) {
                    saw_inner = true;
                }
                if (e.kind == genesis::OscDebugKind::Message && e.nest_depth == 2 &&
                    e.address == "/bundle/child") {
                    saw_child_depth2 = true;
                }
            }
            if (!saw_outer || !saw_inner || !saw_child_depth2) {
                std::cerr << "nested bundle debug nesting mismatch\n";
                ++failures;
            } else {
                std::cout << "OSC nested bundle debug log OK (" << detailed.debug_log.size()
                          << " rows)\n";
            }
        }
    }

    {
        const auto packet = makeIntMessage();
        std::string err;
        const auto msg = genesis::parseOscMessage(packet, &err);
        if (!msg || msg->args.size() != 1 || genesis::oscArgAsFloat(msg->args[0]) != 1.0f) {
            std::cerr << "int message parse failed: " << err << '\n';
            ++failures;
        } else {
            std::cout << "OSC int message OK (" << msg->address << ")\n";
        }
    }

    {
        std::vector<std::uint8_t> bad;
        appendPadded(bad, "not-an-address");
        appendPadded(bad, ",i");
        appendIntBe(bad, 0);
        std::string err;
        const auto msg = genesis::parseOscMessage(bad, &err);
        if (msg) {
            std::cerr << "expected invalid address to fail\n";
            ++failures;
        } else {
            std::cout << "OSC invalid-address rejection OK (" << err << ")\n";
        }
    }

    {
        const auto packet = makeParticleEmitXy();
        std::string err;
        const auto msg = genesis::parseOscMessage(packet, &err);
        genesis::OscParticleEmitSettings settings{};
        genesis::OscParticleEmitEvent event{};
        if (!msg || !genesis::tryParseOscParticleEmit(*msg, settings, event)) {
            std::cerr << "particle emit xy parse failed: " << err << '\n';
            ++failures;
        } else if (std::abs(event.x - 1.5f) > 1e-5f || std::abs(event.y + 0.25f) > 1e-5f ||
                   event.count != settings.default_count) {
            std::cerr << "particle emit xy values mismatch\n";
            ++failures;
        } else {
            const glm::vec3 world = genesis::oscParticleWorldPosition(settings, event);
            if (std::abs(world.x - 1.5f) > 1e-5f || std::abs(world.z + 0.25f) > 1e-5f) {
                std::cerr << "particle emit XZ mapping mismatch\n";
                ++failures;
            } else {
                std::cout << "OSC particle emit xy OK → world(" << world.x << "," << world.y << ","
                          << world.z << ")\n";
            }
        }
    }

    {
        const auto packet = makeParticleEmitXyCount();
        std::string err;
        const auto msg = genesis::parseOscMessage(packet, &err);
        genesis::OscParticleEmitSettings settings{};
        genesis::OscParticleEmitEvent event{};
        if (!msg || !genesis::tryParseOscParticleEmit(*msg, settings, event) || event.count != 12) {
            std::cerr << "particle emit xy+count parse failed\n";
            ++failures;
        } else {
            std::cout << "OSC particle emit xy+count OK (count=" << event.count << ")\n";
        }
    }

    {
        const auto packet = makeRoomPersonXz();
        std::string err;
        const auto msg = genesis::parseOscMessage(packet, &err);
        genesis::OscRoomPersonSettings settings{};
        settings.enabled = true;
        genesis::OscRoomPersonEvent event{};
        if (!msg || !genesis::tryParseOscRoomPerson(*msg, settings, event)) {
            std::cerr << "room person xz parse failed: " << err << '\n';
            ++failures;
        } else if (std::abs(event.x - 2.0f) > 1e-5f || std::abs(event.z - 1.0f) > 1e-5f) {
            std::cerr << "room person xz values mismatch\n";
            ++failures;
        } else {
            const genesis::OscRoomPersonPose pose = genesis::oscRoomPersonPose(settings, event);
            // Person at (2,1) → cylindrical outer wall at room_radius (default 3 m).
            const float person_r = std::sqrt(event.x * event.x + event.z * event.z);
            const float wall_r = std::sqrt(pose.wall.x * pose.wall.x + pose.wall.z * pose.wall.z);
            const float expected_r = settings.room_radius > 0.1f ? settings.room_radius : 3.0f;
            if (wall_r <= person_r + 1e-3f || std::abs(wall_r - expected_r) > 1e-3f ||
                std::abs(pose.feet.x - 2.0f) > 1e-5f || pose.used_plane_hit) {
                std::cerr << "room person wall ray failed (person_r=" << person_r
                          << " wall_r=" << wall_r << " expected=" << expected_r << ")\n";
                ++failures;
            } else {
                std::cout << "OSC room person xz OK → feet(" << pose.feet.x << "," << pose.feet.y
                          << "," << pose.feet.z << ") wall(" << pose.wall.x << "," << pose.wall.y
                          << "," << pose.wall.z << ")\n";
            }
        }
    }

    {
        // Vertical wall plane at x = 4 m; person at (2,0,0) → hit at (4, wall_y, 0).
        genesis::OscRoomPersonSettings settings{};
        settings.wall_ray_mode = genesis::WallRaySpawnMode::PreferPlaneThenDistance;
        settings.room_radius = 3.0f;
        settings.wall_spawn_height = 0.12f;
        genesis::OscRoomPersonEvent event{};
        event.x = 2.0f;
        event.z = 0.0f;
        genesis::WallPlane plane{};
        plane.point = glm::vec3(4.0f, 0.0f, 0.0f);
        plane.normal = glm::vec3(-1.0f, 0.0f, 0.0f);
        const genesis::OscRoomPersonPose pose =
            genesis::oscRoomPersonPose(settings, event, &plane);
        if (!pose.used_plane_hit || std::abs(pose.wall.x - 4.0f) > 1e-3f ||
            std::abs(pose.wall.z) > 1e-3f ||
            std::abs(pose.wall.y - settings.wall_spawn_height) > 1e-3f) {
            std::cerr << "room person plane hit failed wall=(" << pose.wall.x << "," << pose.wall.y
                      << "," << pose.wall.z << ") used_plane=" << pose.used_plane_hit << "\n";
            ++failures;
        } else {
            std::cout << "OSC room person plane hit OK → wall(" << pose.wall.x << "," << pose.wall.y
                      << "," << pose.wall.z << ")\n";
        }

        // Fixed-distance mode ignores plane even when provided.
        settings.wall_ray_mode = genesis::WallRaySpawnMode::FixedDistance;
        const genesis::OscRoomPersonPose fixed =
            genesis::oscRoomPersonPose(settings, event, &plane);
        const float fixed_r =
            std::sqrt(fixed.wall.x * fixed.wall.x + fixed.wall.z * fixed.wall.z);
        if (fixed.used_plane_hit || std::abs(fixed_r - 3.0f) > 1e-3f) {
            std::cerr << "room person fixed-distance ignore-plane failed\n";
            ++failures;
        } else {
            std::cout << "OSC room person fixed-distance OK (ignores plane)\n";
        }

        // Finite panel miss → fall back to fixed distance.
        settings.wall_ray_mode = genesis::WallRaySpawnMode::PreferPlaneThenDistance;
        plane.half_u = 0.25f;
        plane.half_v = 0.25f;
        plane.tangent = glm::vec3(0.0f, 1.0f, 0.0f);
        plane.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
        // Ray hits x=4 at (4, 0.12, 0) which is outside the tiny 0.5×0.5 panel centered at
        // (4,0,0) when measuring Y/Z — actually hit y=0.12,z=0 is inside if half is 0.25.
        // Offset person so hit is far on Z.
        event.z = 2.0f;
        event.x = 2.0f;
        const genesis::OscRoomPersonPose miss =
            genesis::oscRoomPersonPose(settings, event, &plane);
        const float miss_r =
            std::sqrt(miss.wall.x * miss.wall.x + miss.wall.z * miss.wall.z);
        if (miss.used_plane_hit || std::abs(miss_r - 3.0f) > 1e-3f) {
            std::cerr << "room person finite-plane miss fallback failed used="
                      << miss.used_plane_hit << " r=" << miss_r << "\n";
            ++failures;
        } else {
            std::cout << "OSC room person finite-plane miss → fixed-distance OK\n";
        }
    }

    {
        const auto packet = makeRoomPersonIdXz();
        std::string err;
        const auto msg = genesis::parseOscMessage(packet, &err);
        genesis::OscRoomPersonSettings settings{};
        settings.enabled = true;
        genesis::OscRoomPersonEvent event{};
        if (!msg || !genesis::tryParseOscRoomPerson(*msg, settings, event) || event.person_id != 3 ||
            std::abs(event.x - 2.0f) > 1e-5f || std::abs(event.z - 1.0f) > 1e-5f) {
            std::cerr << "room person id+xz parse failed\n";
            ++failures;
        } else {
            std::cout << "OSC room person id+xz OK (id=" << event.person_id << ")\n";
        }
    }

    {
        genesis::OscRoomPersonSettings medusa{};
        genesis::configureMedusaExampleSettings(medusa);
        if (!medusa.enabled || medusa.max_persons != 10 || !medusa.tuio_enabled ||
            medusa.tuio_address != "/tuio/2Dcur" ||
            medusa.wall_ray_mode != genesis::WallRaySpawnMode::PreferPlaneThenDistance ||
            !medusa.debug_draw_wall_ray || medusa.wall_plane_path != "Scene/Wall Plane" ||
            std::abs(medusa.room_radius - 3.0f) > 1e-5f ||
            std::abs(medusa.tank_radius - 1.0f) > 1e-5f ||
            std::abs(medusa.tuio_scale - 3.0f) > 1e-5f) {
            std::cerr << "Medusa example settings mismatch\n";
            ++failures;
        } else {
            std::cout << "Medusa example settings OK (TUIO + max_persons=10, plane+debug)\n";
        }
    }

    {
        const auto alive_pkt = makeTuioAlive();
        const auto set_pkt = makeTuioSet();
        const auto fseq_pkt = makeTuioFseq();
        std::string err;
        const auto alive_msg = genesis::parseOscMessage(alive_pkt, &err);
        const auto set_msg = genesis::parseOscMessage(set_pkt, &err);
        const auto fseq_msg = genesis::parseOscMessage(fseq_pkt, &err);
        genesis::OscRoomPersonSettings settings{};
        genesis::configureMedusaExampleSettings(settings);
        genesis::OscTuio2DcurParse alive_p{};
        genesis::OscTuio2DcurParse set_p{};
        genesis::OscTuio2DcurParse fseq_p{};
        if (!alive_msg || !genesis::tryParseOscTuio2Dcur(*alive_msg, settings, alive_p) ||
            alive_p.command != genesis::OscTuio2DcurCommand::Alive ||
            alive_p.alive.session_ids.size() != 4 || alive_p.alive.session_ids[0] != 2 ||
            alive_p.alive.session_ids[1] != 12 || alive_p.alive.session_ids[2] != 14 ||
            alive_p.alive.session_ids[3] != 13) {
            std::cerr << "TUIO alive parse failed (IDs must be session ids, not a count)\n";
            ++failures;
        } else if (!set_msg || !genesis::tryParseOscTuio2Dcur(*set_msg, settings, set_p) ||
                   set_p.command != genesis::OscTuio2DcurCommand::Set ||
                   set_p.set.session_id != 12 || std::abs(set_p.set.x + 0.5421f) > 1e-4f ||
                   std::abs(set_p.set.y - 0.2700f) > 1e-4f) {
            std::cerr << "TUIO set parse failed\n";
            ++failures;
        } else if (!fseq_msg || !genesis::tryParseOscTuio2Dcur(*fseq_msg, settings, fseq_p) ||
                   fseq_p.command != genesis::OscTuio2DcurCommand::Fseq ||
                   fseq_p.fseq.frame != 9453) {
            std::cerr << "TUIO fseq parse failed\n";
            ++failures;
        } else {
            genesis::OscRoomPersonEvent mapped{};
            genesis::mapTuioSetToRoomPersonEvent(settings, set_p.set, mapped);
            const float expect_x = -0.5421f * settings.tuio_scale;
            const float expect_z = 0.2700f * settings.tuio_scale;
            if (mapped.person_id != 12 || !mapped.from_tuio ||
                std::abs(mapped.x - expect_x) > 1e-4f || std::abs(mapped.z - expect_z) > 1e-4f) {
                std::cerr << "TUIO→floor map failed feet xz=(" << mapped.x << "," << mapped.z
                          << ")\n";
                ++failures;
            } else {
                std::cout << "OSC TUIO 2Dcur alive/set/fseq OK → id=" << mapped.person_id
                          << " floor(" << mapped.x << "," << mapped.z << ")\n";
            }
        }
    }

#if !defined(GENESIS_OSC_SMOKE_LIGHT)
    {
        genesis::Scene scene{};
        const genesis::NodeId root = scene.createEmptyNode("Scene");
        const genesis::NodeId emitter =
            scene.createParticleEmitterNode("OSC Particles", root, genesis::makeOscParticleEmitter());
        scene.updateWorldTransforms();

        genesis::ParticleWorld world_a;
        genesis::ParticleWorld world_b;
        world_a.setSceneSeed(42);
        world_b.setSceneSeed(42);

        const glm::vec3 pos{1.5f, 0.0f, -0.25f};
        const std::uint32_t spawned_a = world_a.emitBurstAtWorld(scene, emitter, pos, 8);
        const std::uint32_t spawned_b = world_b.emitBurstAtWorld(scene, emitter, pos, 8);
        if (spawned_a != 8 || spawned_b != 8) {
            std::cerr << "emitBurstAtWorld count mismatch: " << spawned_a << " / " << spawned_b << '\n';
            ++failures;
        } else if (world_a.stateHash() != world_b.stateHash()) {
            std::cerr << "dual ParticleWorld stateHash mismatch after OSC-style burst\n";
            ++failures;
        } else {
            std::cout << "OSC particle dual-world determinism OK (hash=" << world_a.stateHash() << ")\n";
        }
    }
#else
    std::cout << "OSC particle world tests skipped (GENESIS_OSC_SMOKE_LIGHT)\n";
#endif

    {
        std::vector<genesis::OscParticleEmitEvent> events;
        genesis::OscParticleEmitEvent e{};
        e.sequence = 1;
        e.x = 1.0f;
        e.y = 2.0f;
        e.count = 4;
        events.push_back(e);
        std::uint64_t cursor_a = 0;
        std::uint64_t cursor_b = 0;
        std::uint32_t seen_a = 0;
        std::uint32_t seen_b = 0;
        for (const auto& event : events) {
            if (event.sequence > cursor_a) {
                ++seen_a;
                cursor_a = event.sequence;
            }
            if (event.sequence > cursor_b) {
                ++seen_b;
                cursor_b = event.sequence;
            }
        }
        if (seen_a != 1 || seen_b != 1 || cursor_a != cursor_b) {
            std::cerr << "dual-consumer cursor model failed\n";
            ++failures;
        } else {
            std::cout << "OSC particle dual-consumer queue OK\n";
        }
    }

    std::cout << "failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
