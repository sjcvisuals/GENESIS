#pragma once

#include <cstdint>

namespace genesis {

// Counter-based deterministic RNG (PCG-ish). Same inputs → same float in [0,1).
inline std::uint32_t particleHashU32(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return static_cast<std::uint32_t>(x);
}

inline std::uint64_t particleMixSeed(std::uint64_t scene_seed, std::uint64_t emitter_id,
                                     std::uint64_t emit_index, std::uint32_t channel) {
    std::uint64_t h = scene_seed;
    h ^= emitter_id + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h ^= emit_index + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h ^= static_cast<std::uint64_t>(channel) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

inline float particleRandom01(std::uint64_t scene_seed, std::uint64_t emitter_id,
                              std::uint64_t emit_index, std::uint32_t channel) {
    const std::uint32_t u = particleHashU32(particleMixSeed(scene_seed, emitter_id, emit_index, channel));
    return static_cast<float>(u >> 8) * (1.0f / 16777216.0f);
}

inline float particleRandomRange(std::uint64_t scene_seed, std::uint64_t emitter_id,
                                 std::uint64_t emit_index, std::uint32_t channel, float min_v,
                                 float max_v) {
    return min_v + (max_v - min_v) * particleRandom01(scene_seed, emitter_id, emit_index, channel);
}

} // namespace genesis
