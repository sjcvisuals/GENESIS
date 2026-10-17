#pragma once

// Compact over-life curves (Niagara-style Size/Color over Life without a full curve atlas).
// Fixed key slots keep emitters POD-serializable and cache-friendly.

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/vec4.hpp>

namespace genesis {

struct ParticleCurveKey1 {
    float t = 0.0f; // normalized lifetime [0,1]
    float v = 1.0f;
};

struct ParticleCurveKey4 {
    float t = 0.0f;
    glm::vec4 v{1.0f};
};

struct ParticleCurve1 {
    static constexpr std::size_t kMaxKeys = 4;
    std::array<ParticleCurveKey1, kMaxKeys> keys{};
    std::uint8_t count = 0;

    static ParticleCurve1 linear(float a, float b) {
        ParticleCurve1 c{};
        c.count = 2;
        c.keys[0] = {0.0f, a};
        c.keys[1] = {1.0f, b};
        return c;
    }

    float evaluate(float t) const {
        if (count == 0) {
            return 1.0f;
        }
        t = std::clamp(t, 0.0f, 1.0f);
        if (count == 1 || t <= keys[0].t) {
            return keys[0].v;
        }
        for (std::uint8_t i = 1; i < count; ++i) {
            if (t <= keys[i].t) {
                const float t0 = keys[i - 1].t;
                const float t1 = keys[i].t;
                const float u = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
                return keys[i - 1].v + (keys[i].v - keys[i - 1].v) * u;
            }
        }
        return keys[count - 1].v;
    }
};

struct ParticleCurveColor {
    static constexpr std::size_t kMaxKeys = 4;
    std::array<ParticleCurveKey4, kMaxKeys> keys{};
    std::uint8_t count = 0;

    static ParticleCurveColor linear(const glm::vec4& a, const glm::vec4& b) {
        ParticleCurveColor c{};
        c.count = 2;
        c.keys[0] = {0.0f, a};
        c.keys[1] = {1.0f, b};
        return c;
    }

    glm::vec4 evaluate(float t) const {
        if (count == 0) {
            return glm::vec4(1.0f);
        }
        t = std::clamp(t, 0.0f, 1.0f);
        if (count == 1 || t <= keys[0].t) {
            return keys[0].v;
        }
        for (std::uint8_t i = 1; i < count; ++i) {
            if (t <= keys[i].t) {
                const float t0 = keys[i - 1].t;
                const float t1 = keys[i].t;
                const float u = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
                return keys[i - 1].v + (keys[i].v - keys[i - 1].v) * u;
            }
        }
        return keys[count - 1].v;
    }
};

} // namespace genesis
