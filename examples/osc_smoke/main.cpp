// Headless OSC parser smoke test — validates message + bundle decoding.

#include "protocol/OscParser.hpp"

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
        // Intentionally invalid address (missing leading '/').
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

    std::cout << "failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
