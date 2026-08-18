#pragma once

#include "WinPrefix.h"
#include "RemoverFrameDebug.h"
#include "matting/BgRemover.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace rxext::genesis_remover {

// Background matte: preload CPU RVM first, then Map our capture share (never Video In).
// Releases the shared texture before ORT infer so PIXERA's GPU is not contended.
class MatteCaptureWorker {
public:
    explicit MatteCaptureWorker(std::uint64_t adapter_luid);
    ~MatteCaptureWorker();

    MatteCaptureWorker(const MatteCaptureWorker&) = delete;
    MatteCaptureWorker& operator=(const MatteCaptureWorker&) = delete;

    void setCaptureShare(void* nt_share_handle, std::uint32_t width, std::uint32_t height);
    void setQuality(int quality_0_to_2);
    bool engineReady() const { return engine_ready_.load(); }
    bool kick();
    bool busy() const { return busy_.load(); }
    bool tryTakeMatte(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_w, std::uint32_t& out_h,
                      ::genesis::RemoverFrameDebug* dbg);

private:
    void threadMain();
    bool ensureWorkerDevice();
    bool ensureRemover();
    bool openCaptureLocked();
    bool captureAndMatteOnce();
    void releaseOpened();
    void releaseDevice();

    std::uint64_t adapter_luid_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> kick_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> engine_ready_{false};
    std::atomic<int> quality_{0}; // Fast by default for PIXERA CPU path

    std::mutex in_mu_;
    void* capture_handle_ = nullptr;
    std::uint32_t in_w_ = 0;
    std::uint32_t in_h_ = 0;
    std::uint64_t in_gen_ = 0;

    std::mutex out_mu_;
    std::vector<std::uint8_t> matte_rgba_;
    std::uint32_t out_w_ = 0;
    std::uint32_t out_h_ = 0;
    ::genesis::RemoverFrameDebug out_dbg_{};
    bool out_ready_ = false;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* opened_capture_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    void* opened_handle_ = nullptr;
    std::uint32_t opened_w_ = 0;
    std::uint32_t opened_h_ = 0;
    std::uint64_t opened_gen_ = 0;

    ::genesis::BgRemover remover_;
    bool remover_ready_ = false;
    int remover_quality_ = -1;
    std::vector<std::uint8_t> scratch_in_;
    std::vector<std::uint8_t> scratch_out_;
    std::uint64_t captures_ = 0;
    std::uint64_t nonzero_captures_ = 0;
};

} // namespace rxext::genesis_remover
