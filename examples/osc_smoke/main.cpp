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
        genesis::OscRoomPersonEvent event{};
        if (!msg || !genesis::tryParseOscRoomPerson(*msg, settings, event)) {
            std::cerr << "room person xz parse failed: " << err << '\n';
            ++failures;
        } else if (std::abs(event.x - 2.0f) > 1e-5f || std::abs(event.z - 1.0f) > 1e-5f) {
            std::cerr << "room person xz values mismatch\n";
            ++failures;
        } else {
            const genesis::OscRoomPersonPose pose = genesis::oscRoomPersonPose(settings, event);
            // Person at (2,1) → direction toward +X/+Z; wall should be outside the person.
            const float person_r = std::sqrt(event.x * event.x + event.z * event.z);
            const float wall_r = std::sqrt(pose.wall.x * pose.wall.x + pose.wall.z * pose.wall.z);
            if (wall_r <= person_r + 1e-3f || std::abs(pose.feet.x - 2.0f) > 1e-5f) {
                std::cerr << "room person wall ray failed (person_r=" << person_r
                          << " wall_r=" << wall_r << ")\n";
                ++failures;
            } else {
                std::cout << "OSC room person xz OK → feet(" << pose.feet.x << "," << pose.feet.y
                          << "," << pose.feet.z << ") wall(" << pose.wall.x << "," << pose.wall.y
                          << "," << pose.wall.z << ")\n";
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
