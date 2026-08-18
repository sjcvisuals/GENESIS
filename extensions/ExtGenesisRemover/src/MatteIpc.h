#pragma once

#include <cstdint>

// Shared between ExtGenesisRemover.dll and GenesisRemoverMatteHost.exe.
// Named file mapping: Local\GenesisRemoverMatteIpc_v44
// Named event:       Local\GenesisRemoverMatteKick_v44
namespace genesis::matte_ipc {

inline constexpr char kMappingName[] = "Local\\GenesisRemoverMatteIpc_v44";
inline constexpr char kKickEventName[] = "Local\\GenesisRemoverMatteKick_v44";
inline constexpr char kStopEventName[] = "Local\\GenesisRemoverMatteStop_v44";

inline constexpr std::uint32_t kMagic = 0x34444d47u; // GMD4
inline constexpr std::uint32_t kVersion = 1;
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
    std::uint32_t quality = 0; // 0=Fast 1=Balanced 2=Quality
    std::uint64_t kick_seq = 0;
    std::uint64_t done_seq = 0;
    std::uint32_t result_nz = 0;
    std::uint32_t result_ok = 0;
    char stage[32]{};
    char status[96]{};
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
