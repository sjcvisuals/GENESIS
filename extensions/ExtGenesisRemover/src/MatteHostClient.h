#pragma once

#include "WinPrefix.h"
#include "MatteIpc.h"
#include "RemoverFrameDebug.h"

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace rxext::genesis_remover {

class D3d11SharedTexture;

// Launches/attaches GenesisRemoverMatteHost.exe (ORT lives there, not in PIXERA).
// DLL only blits Video In -> capture share and reads RGBA results from shared memory.
class MatteHostClient {
public:
    MatteHostClient();
    ~MatteHostClient();

    MatteHostClient(const MatteHostClient&) = delete;
    MatteHostClient& operator=(const MatteHostClient&) = delete;

    bool ensureStarted(std::uint64_t adapter_luid);
    bool hostReady() const;
    // quality: 0=Fast 1=Balanced 2=Quality 3=Custom (downsample + max_infer_side).
    void setQuality(int quality_0_to_3, float custom_downsample = 0.0f,
                    std::uint32_t custom_max_infer_side = 0);
    // Blit already done into capture; publish handle + kick host.
    bool kickCapture(D3d11SharedTexture& capture, std::uint32_t width, std::uint32_t height);
    bool tryTakeMatte(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_w, std::uint32_t& out_h,
                      ::genesis::RemoverFrameDebug* dbg);
    bool busy() const;
    // If host wedged (kick never acked), clear local busy so we can kick again.
    void clearBusyIfStale(std::uint32_t stale_ms = 1500);
    // Host writes ep name into IPC status (e.g. "DML:1" or "CPU").
    std::string hostEp() const;
    // Last host cycle / infer times (ms); 0 if unknown.
    float hostCycleMs() const;
    float hostInferMs() const;
    // Kill + relaunch host (recover from GPU thrash after outputs).
    bool restartHost(const char* reason);

private:
    bool openIpc();
    void closeIpc();
    bool launchHost();
    bool duplicateCaptureHandle(void* local_nt_handle);
    void writeQualityToHeader();

    void* mapping_ = nullptr; // HANDLE
    void* view_ = nullptr;    // Header*
    void* kick_event_ = nullptr;
    void* stop_event_ = nullptr;
    void* host_process_ = nullptr;
    std::uint64_t adapter_luid_ = 0;
    std::uint64_t last_done_seq_ = 0;
    std::uint64_t last_dup_handle_ = 0;
    std::uint32_t last_kick_tick_ms_ = 0;
    int quality_ = 0;
    float custom_downsample_ = 0.0f;
    std::uint32_t custom_max_infer_side_ = 0;
    bool started_ = false;
};

} // namespace rxext::genesis_remover
