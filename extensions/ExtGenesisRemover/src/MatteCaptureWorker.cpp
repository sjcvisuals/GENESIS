#include "WinPrefix.h"
#include "MatteCaptureWorker.h"

#include "D3d11SharedTexture.h"
#include "RemoverLog.h"

#include "d3d11/ComPointer.h"

#include <d3d11_1.h>
#include <dxgi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <string_view>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace rxext::genesis_remover {
namespace {

LUID toLuid(std::uint64_t value) {
    LUID luid{};
    luid.LowPart = static_cast<DWORD>(value & 0xffffffffu);
    luid.HighPart = static_cast<LONG>(value >> 32);
    return luid;
}

ComPointer<IDXGIAdapter1> findAdapter(IDXGIFactory1& factory, LUID want) {
    ComPointer<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory.EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (want.HighPart == 0 && want.LowPart == 0) {
            return adapter;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.AdapterLuid.HighPart == want.HighPart && desc.AdapterLuid.LowPart == want.LowPart) {
            return adapter;
        }
        adapter.release();
    }
    return {};
}

} // namespace

MatteCaptureWorker::MatteCaptureWorker(std::uint64_t adapter_luid) : adapter_luid_(adapter_luid) {
    thread_ = std::thread([this] { threadMain(); });
    removerLog(std::format("MatteCaptureWorker started adapter={:016x} (capture-share mode)",
                           adapter_luid_));
}

MatteCaptureWorker::~MatteCaptureWorker() {
    stop_.store(true);
    kick_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    releaseOpened();
    releaseDevice();
}

void MatteCaptureWorker::setCaptureShare(void* nt_share_handle, std::uint32_t width,
                                         std::uint32_t height) {
    std::lock_guard<std::mutex> lock(in_mu_);
    if (capture_handle_ == nt_share_handle && in_w_ == width && in_h_ == height) {
        return;
    }
    capture_handle_ = nt_share_handle;
    in_w_ = width;
    in_h_ = height;
    ++in_gen_;
}

void MatteCaptureWorker::setQuality(int quality_0_to_2) {
    quality_.store(std::clamp(quality_0_to_2, 0, 2));
}

void MatteCaptureWorker::kick() {
    if (busy_.load()) {
        return;
    }
    kick_.store(true);
}

bool MatteCaptureWorker::tryTakeMatte(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_w,
                                      std::uint32_t& out_h, ::genesis::RemoverFrameDebug* dbg) {
    std::lock_guard<std::mutex> lock(out_mu_);
    if (!out_ready_) {
        return false;
    }
    out_rgba.swap(matte_rgba_);
    out_w = out_w_;
    out_h = out_h_;
    if (dbg) {
        *dbg = out_dbg_;
    }
    out_ready_ = false;
    return !out_rgba.empty() && out_w > 0 && out_h > 0;
}

void MatteCaptureWorker::releaseOpened() {
    if (staging_) {
        staging_->Release();
        staging_ = nullptr;
    }
    if (opened_capture_) {
        opened_capture_->Release();
        opened_capture_ = nullptr;
    }
    opened_handle_ = nullptr;
    opened_w_ = 0;
    opened_h_ = 0;
    opened_gen_ = 0;
}

void MatteCaptureWorker::releaseDevice() {
    pipeline_.reset();
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

bool MatteCaptureWorker::ensureWorkerDevice() {
    if (device_ && context_) {
        return true;
    }
    ComPointer<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        removerLog("MatteCaptureWorker: CreateDXGIFactory1 failed");
        return false;
    }
    ComPointer<IDXGIAdapter1> adapter = findAdapter(*factory, toLuid(adapter_luid_));
    if (!adapter) {
        removerLog("MatteCaptureWorker: adapter not found");
        return false;
    }
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1,
                                 D3D11_SDK_VERSION, &device_, nullptr, &context_)) ||
        !device_ || !context_) {
        removerLog("MatteCaptureWorker: D3D11CreateDevice failed");
        releaseDevice();
        return false;
    }
    pipeline_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
    removerLog("MatteCaptureWorker D3D device ready (opens capture share only, never Video In)");
    return true;
}

bool MatteCaptureWorker::openCaptureLocked() {
    void* handle = nullptr;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::uint64_t gen = 0;
    {
        std::lock_guard<std::mutex> lock(in_mu_);
        handle = capture_handle_;
        w = in_w_;
        h = in_h_;
        gen = in_gen_;
    }
    if (!handle || w == 0 || h == 0) {
        return false;
    }
    if (opened_handle_ == handle && opened_gen_ == gen && opened_capture_ && staging_) {
        return true;
    }
    releaseOpened();

    ID3D11Device1* device1 = nullptr;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&device1))) || !device1) {
        removerLog("MatteCaptureWorker: ID3D11Device1 QI failed");
        return false;
    }
    ID3D11Texture2D* tex = nullptr;
    const HRESULT open_hr = device1->OpenSharedResource1(handle, IID_PPV_ARGS(&tex));
    device1->Release();
    if (FAILED(open_hr) || !tex) {
        removerLog(std::format("MatteCaptureWorker OpenSharedResource1(capture) hr=0x{:08x}",
                               static_cast<unsigned long>(open_hr)));
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device_->CreateTexture2D(&staging_desc, nullptr, &staging)) || !staging) {
        tex->Release();
        removerLog("MatteCaptureWorker staging CreateTexture2D failed");
        return false;
    }

    opened_capture_ = tex;
    staging_ = staging;
    opened_handle_ = handle;
    opened_w_ = w;
    opened_h_ = h;
    opened_gen_ = gen;
    if (pipeline_) {
        pipeline_->resize(w, h);
    }
    removerLog(std::format("MatteCaptureWorker opened CAPTURE share {}x{} (not Video In)", w, h));
    return true;
}

bool MatteCaptureWorker::captureAndMatteOnce() {
    busy_.store(true);
    struct BusyClear {
        std::atomic<bool>& b;
        ~BusyClear() { b.store(false); }
    } busy_clear{busy_};

    if (!ensureWorkerDevice() || !openCaptureLocked() || !opened_capture_ || !staging_) {
        return false;
    }

    context_->CopyResource(staging_, opened_capture_);
    {
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_EVENT;
        ID3D11Query* q = nullptr;
        if (SUCCEEDED(device_->CreateQuery(&qd, &q)) && q) {
            context_->End(q);
            while (context_->GetData(q, nullptr, 0, 0) == S_FALSE) {
                Sleep(0);
            }
            q->Release();
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map_hr = context_->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(map_hr) || !mapped.pData) {
        removerLog(std::format("MatteCaptureWorker Map capture failed hr=0x{:08x}",
                               static_cast<unsigned long>(map_hr)));
        return false;
    }

    const std::size_t bytes = static_cast<std::size_t>(opened_w_) * opened_h_ * 4u;
    scratch_in_.resize(bytes);
    auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::uint32_t y = 0; y < opened_h_; ++y) {
        std::memcpy(scratch_in_.data() + static_cast<std::size_t>(y) * opened_w_ * 4u,
                    src + static_cast<std::size_t>(y) * mapped.RowPitch, opened_w_ * 4u);
    }
    context_->Unmap(staging_, 0);
    ++captures_;

    if (captures_ <= 5u || (captures_ % 30u) == 1u) {
        D3d11SharedTexture::logFrameStats(scratch_in_, opened_w_, opened_h_, "worker_capture");
    }

    bool any = false;
    for (std::uint8_t b : scratch_in_) {
        if (b) {
            any = true;
            break;
        }
    }
    if (!any) {
        if (captures_ <= 5u || (captures_ % 60u) == 1u) {
            removerLog("worker_capture nz=0");
        }
        return false;
    }
    ++nonzero_captures_;

    if (!pipeline_) {
        return false;
    }
    ::genesis::BgRemoverSettings settings = pipeline_->remover().settings();
    settings.backend = ::genesis::BgRemoverBackend::Auto;
    settings.quality = static_cast<::genesis::BgRemoverQuality>(quality_.load());
    settings.rvm_downsample = 0.0f;
    settings.temporal_alpha = 0.20f;
    pipeline_->setSettings(settings);
    if (!pipeline_->asyncEnabled()) {
        pipeline_->setAsyncEnabled(true);
    }

    if (captures_ <= 3u) {
        removerLog("worker RVM process begin");
    }
    ::genesis::RemoverFrameDebug dbg{};
    std::string error;
    (void)pipeline_->processCpuRgbaFrame(scratch_in_.data(), true, &error, &dbg, false,
                                         &scratch_out_);
    if (captures_ <= 3u) {
        removerLog(std::format("worker RVM process end stage={} nz={} err={}",
                               dbg.stage ? dbg.stage : "?", dbg.nonzero_rgb, error));
    }

    if (!(dbg.stage && std::string_view(dbg.stage) == "matte" && dbg.nonzero_rgb > 0 &&
          !scratch_out_.empty())) {
        // Do NOT publish passthrough to primary — that replaced the live Video In blit with a
        // single stale upload and looked frozen. Keep capturing until RVM returns matte.
        if (captures_ <= 5u || (captures_ % 30u) == 1u) {
            removerLog(std::format("worker matte pending stage={} nz={} (live blit continues)",
                                   dbg.stage ? dbg.stage : "?", dbg.nonzero_rgb));
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(out_mu_);
        matte_rgba_ = scratch_out_;
        out_w_ = opened_w_;
        out_h_ = opened_h_;
        out_dbg_ = dbg;
        out_ready_ = true;
    }
    removerLog(std::format("worker matte_ok nz={} captures={}/{}", dbg.nonzero_rgb,
                           nonzero_captures_, captures_));
    return true;
}

void MatteCaptureWorker::threadMain() {
    removerLog("MatteCaptureWorker thread running");
    while (!stop_.load()) {
        if (!kick_.exchange(false)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (stop_.load()) {
            break;
        }
        try {
            (void)captureAndMatteOnce();
        } catch (const std::exception& ex) {
            removerLog(std::format("MatteCaptureWorker exception: {}", ex.what()));
            busy_.store(false);
        }
    }
    releaseOpened();
    releaseDevice();
    removerLog("MatteCaptureWorker thread exit");
}

} // namespace rxext::genesis_remover
