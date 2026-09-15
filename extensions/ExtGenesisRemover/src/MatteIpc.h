#pragma once

#include <cstddef>
#include <cstdint>

// Shared between ExtGenesisRemover.dll and GenesisRemoverMatteHost.exe.
// Named file mapping: Local\GenesisRemoverMatteIpc_v57
// Named event:       Local\GenesisRemoverMatteKick_v57
// v57: vision_effect / result_as / model_path for multi-model ONNX (ExtOnnxVision-style).
// v61: dll/host build stamps + version 7 for demo mismatch detection.
// v62: capture_in_shm - DLL uploads capture RGBA; host must NOT Map shared GPU tex.
// v63: debug harness (HEALTH lines, FAIL file, one-shot health-check script).
// v63c: Flush after async capture Copy/End (fixes ~3s download_ms + capture_nz0).
// v63d: Live kick pacing (~50ms floor) + 480px capture once download is healthy.
// v63e: Output recover - abandon stuck async downloads, skip black clear (flicker),
//       optional Flush skip + host restart after PIXERA output teardown thrash.
namespace genesis::matte_ipc {

inline constexpr char kMappingName[] = "Local\\GenesisRemoverMatteIpc_v57";
inline constexpr char kKickEventName[] = "Local\\GenesisRemoverMatteKick_v57";
inline constexpr char kStopEventName[] = "Local\\GenesisRemoverMatteStop_v57";

// Single source of truth - must match BUILD_STAMP.txt / Debug Status.
inline constexpr char kBuildStamp[] = "remover-demo-v63e-output-recover-20260915";

inline constexpr std::uint32_t kMagic = 0x34444d47u; // GMD4
inline constexpr std::uint32_t kVersion = 8;
inline constexpr std::uint32_t kMaxWidth = 1920;
inline constexpr std::uint32_t kMaxHeight = 1080;
inline constexpr std::uint32_t kMaxBytes = kMaxWidth * kMaxHeight * 4u;
inline constexpr std::uint32_t kModelPathBytes = 260;
inline constexpr std::uint32_t kStampBytes = 64;

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t adapter_luid = 0;
    std::uint64_t capture_handle = 0;
    std::uint32_t host_pid = 0;
    std::uint32_t host_ready = 0;
    std::uint32_t dll_pid = 0;
    std::uint32_t quality = 0;
    std::uint64_t kick_seq = 0;
    std::uint64_t done_seq = 0;
    std::uint32_t result_nz = 0;
    std::uint32_t result_ok = 0;
    char stage[32]{};
    char status[96]{};
    float rvm_downsample = 0.0f;
    std::uint32_t max_infer_side = 0;
    float last_cycle_ms = 0.0f;
    float last_infer_ms = 0.0f;
    float last_map_ms = 0.0f;
    std::uint32_t vision_effect = 0;
    std::uint32_t result_as = 0;
    char model_path[kModelPathBytes]{};
    // v61: both sides publish kBuildStamp; DLL flags mismatch for UI/logs.
    char dll_stamp[kStampBytes]{};
    char host_stamp[kStampBytes]{};
    std::uint32_t stamp_mismatch = 0;
    // v62: 1 = pixels[] holds capture RGBA from DLL (host skips OpenShared+Map).
    std::uint32_t capture_in_shm = 0;
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
