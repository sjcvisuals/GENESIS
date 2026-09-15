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
    // True when DLL/host BUILD stamps disagree (mixed install - reinstall both).
    bool stampMismatch() const;
    std::string hostStamp() const;
    std::uint64_t doneSeq() const;
    // quality: 0=Fast 1=Balanced 2=Quality 3=Custom (downsample + max_infer_side).
    void setQuality(int quality_0_to_3, float custom_downsample = 0.0f,
                    std::uint32_t custom_max_infer_side = 0);
    // vision_effect / result_as match genesis::VisionEffect / VisionResultAs.
    // model_path empty -> host uses the effect's default asset path.
    void setVision(int vision_effect, int result_as, const std::string& model_path = {});
    // Blit already done into capture; publish handle + kick host.
    bool kickCapture(D3d11SharedTexture& capture, std::uint32_t width, std::uint32_t height);
    // v62: capture already in CPU RGBA - copy into IPC pixels and kick (no GPU share Map).
    bool kickCaptureRgba(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                        float download_ms);
    bool tryTakeMatte(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_w, std::uint32_t& out_h,
                      ::genesis::RemoverFrameDebug* dbg);
    bool busy() const;
    // If host wedged (kick never acked), clear local busy so we can kick again.
    void clearBusyIfStale(std::uint32_t stale_ms = 1500);
    // Host writes ep name into IPC status (e.g. "DML:1" or "CPU").
    std::string hostEp() const;
    // Last host cycle / infer / map times (ms); 0 if unknown.
    float hostCycleMs() const;
    float hostInferMs() const;
    float hostMapMs() const;
    // True when last kick used CPU RGBA IPC (v62+); false = legacy GPU share Map.
    bool hostCaptureInShm() const;
    // Kill + relaunch host (recover from GPU thrash after outputs).
    // Rate-limited - safe to call from update thread, but prefer not to during output toggles.
    bool restartHost(const char* reason);

private:
    bool openIpc();
    void closeIpc();
    bool launchHost();
    // If IPC already has a live host_pid, OpenProcess and reuse it (no CreateProcess).
    bool adoptRunningHost();
    bool duplicateCaptureHandle(void* local_nt_handle);
    void writeQualityToHeader();
    void writeVisionToHeader();

    void* mapping_ = nullptr; // HANDLE
    void* view_ = nullptr;    // Header*
    void* kick_event_ = nullptr;
    void* stop_event_ = nullptr;
    void* host_process_ = nullptr;
    std::uint32_t last_launch_tick_ms_ = 0;
    std::uint64_t adapter_luid_ = 0;
    std::uint64_t last_done_seq_ = 0;
    std::uint64_t last_dup_handle_ = 0;
    std::uint32_t last_kick_tick_ms_ = 0;
    int quality_ = 0;
    float custom_downsample_ = 0.0f;
    std::uint32_t custom_max_infer_side_ = 0;
    int vision_effect_ = 0;
    int result_as_ = 0;
    std::string model_path_;
    bool started_ = false;
};

} // namespace rxext::genesis_remover
