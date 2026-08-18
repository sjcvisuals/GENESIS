#pragma once

#include "WinPrefix.h"
#include "rxext.h"
#include "runtime/RemoverPipeline.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

namespace rxext::genesis_remover {

class D3d11SharedTexture;
class D3d11Passthrough;

// Captures Video In on a *second* D3D11 device + background thread so the PIXERA
// render callback never Map/CopyResource/RVM (that stalled the fence and froze the feed).
// Worker: OpenSharedResource -> GPU blit to private RGBA8 -> Map -> RVM.
class MatteCaptureWorker {
public:
    explicit MatteCaptureWorker(std::uint64_t adapter_luid);
    ~MatteCaptureWorker();

    MatteCaptureWorker(const MatteCaptureWorker&) = delete;
    MatteCaptureWorker& operator=(const MatteCaptureWorker&) = delete;

    void setVideoIn(void* nt_share_handle, std::uint32_t width, std::uint32_t height, Format format);
    void setQuality(int quality_0_to_2);
    void kick();
    bool tryTakeMatte(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_w, std::uint32_t& out_h,
                      ::genesis::RemoverFrameDebug* dbg);

private:
    void threadMain();
    bool ensureWorkerDevice();
    bool openVideoInLocked();
    bool captureAndMatteOnce();
    void releaseOpened();
    void releaseDevice();

    std::uint64_t adapter_luid_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> kick_{false};
    std::atomic<int> quality_{1};

    std::mutex in_mu_;
    void* video_in_handle_ = nullptr;
    std::uint32_t in_w_ = 0;
    std::uint32_t in_h_ = 0;
    Format in_fmt_ = Format::None;
    std::uint64_t in_gen_ = 0;

    std::mutex out_mu_;
    std::vector<std::uint8_t> matte_rgba_;
    std::uint32_t out_w_ = 0;
    std::uint32_t out_h_ = 0;
    ::genesis::RemoverFrameDebug out_dbg_{};
    bool out_ready_ = false;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* opened_vin_ = nullptr;
    ID3D11ShaderResourceView* opened_srv_ = nullptr;
    void* opened_handle_ = nullptr;
    std::uint32_t opened_w_ = 0;
    std::uint32_t opened_h_ = 0;
    Format opened_fmt_ = Format::None;
    std::uint64_t opened_gen_ = 0;

    std::unique_ptr<D3d11SharedTexture> capture_;
    std::unique_ptr<D3d11Passthrough> blit_;
    std::unique_ptr<::genesis::RemoverPipeline> pipeline_;
    std::vector<std::uint8_t> scratch_in_;
    std::vector<std::uint8_t> scratch_out_;
    std::uint64_t captures_ = 0;
    std::uint64_t nonzero_captures_ = 0;
};

} // namespace rxext::genesis_remover
