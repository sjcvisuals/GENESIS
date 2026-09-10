#pragma once

#include <cstddef>
#include <cstdint>

// Shared between ExtGenesisRemover.dll and GenesisRemoverMatteHost.exe.
// Named file mapping: Local\GenesisRemoverMatteIpc_v55
// Named event:       Local\GenesisRemoverMatteKick_v55
// v55: host publishes last_map_ms (shared-texture readback) for GPU-stall cooldown.
namespace genesis::matte_ipc {

inline constexpr char kMappingName[] = "Local\\GenesisRemoverMatteIpc_v55";
inline constexpr char kKickEventName[] = "Local\\GenesisRemoverMatteKick_v55";
inline constexpr char kStopEventName[] = "Local\\GenesisRemoverMatteStop_v55";

inline constexpr std::uint32_t kMagic = 0x34444d47u; // GMD4
inline constexpr std::uint32_t kVersion = 5;
inline constexpr std::uint32_t kMaxWidth = 1920;
inline constexpr std::uint32_t kMaxHeight = 1080;
inline constexpr std::uint32_t kMaxBytes = kMaxWidth * kMaxHeight * 4u;

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t adapter_luid = 0;
    // Duplicated into the host process — host OpenSharedResource1(this).
    std::uint64_t capture_handle = 0;
    std::uint32_t host_pid = 0;
    std::uint32_t host_ready = 0; // 1 once ORT loaded
    std::uint32_t dll_pid = 0;
    // 0=Fast 1=Balanced 2=Quality 3=Custom (uses rvm_downsample + max_infer_side).
    std::uint32_t quality = 0;
    std::uint64_t kick_seq = 0;
    std::uint64_t done_seq = 0;
    std::uint32_t result_nz = 0;
    std::uint32_t result_ok = 0;
    char stage[32]{};
    char status[96]{};
    // Custom quality (quality==3). downsample 0 → host clamps to a safe default.
    float rvm_downsample = 0.0f;
    std::uint32_t max_infer_side = 0;
    // Host → DLL telemetry (ms). map = shared capture Copy+Map (stalls PIXERA GPU).
    float last_cycle_ms = 0.0f;
    float last_infer_ms = 0.0f;
    float last_map_ms = 0.0f;
    // RGBA8 pixels follow when done_seq updates (tight pack, row pitch = width*4).
};
#pragma pack(pop)

inline constexpr std::size_t kMappingBytes =
    sizeof(Header) + static_cast<std::size_t>(kMaxBytes);

inline std::uint8_t* pixels(Header* h) {
    return reinterpret_cast<std::uint8_t*>(h + 1);
}

inline const std::uint8_t* pixels(const Header* h) {
    return reinterpret_cast<const std::uint8_t*>(h + 1);
}

} // namespace genesis::matte_ipc
