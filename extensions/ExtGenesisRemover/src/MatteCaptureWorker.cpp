#include "WinPrefix.h"
#include "MatteCaptureWorker.h"

#include "D3d11Passthrough.h"
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

DXGI_FORMAT toDxgi(Format format) {
    switch (format) {
    case Format::R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::R16G16B16A16_SFLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32G32B32A32_SFLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

} // namespace

MatteCaptureWorker::MatteCaptureWorker(std::uint64_t adapter_luid) : adapter_luid_(adapter_luid) {
    thread_ = std::thread([this] { threadMain(); });
    removerLog(std::format("MatteCaptureWorker started adapter={:016x}", adapter_luid_));
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

void MatteCaptureWorker::setVideoIn(void* nt_share_handle, std::uint32_t width, std::uint32_t height,
                                    Format format) {
    std::lock_guard<std::mutex> lock(in_mu_);
    if (video_in_handle_ == nt_share_handle && in_w_ == width && in_h_ == height &&
        in_fmt_ == format) {
        return;
    }
    video_in_handle_ = nt_share_handle;
    in_w_ = width;
    in_h_ = height;
    in_fmt_ = format;
    ++in_gen_;
}

void MatteCaptureWorker::setQuality(int quality_0_to_2) {
    quality_.store(std::clamp(quality_0_to_2, 0, 2));
}

void MatteCaptureWorker::kick() {
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
    if (opened_srv_) {
        opened_srv_->Release();
        opened_srv_ = nullptr;
    }
    if (opened_vin_) {
        opened_vin_->Release();
        opened_vin_ = nullptr;
    }
    opened_handle_ = nullptr;
    opened_w_ = 0;
    opened_h_ = 0;
    opened_fmt_ = Format::None;
    opened_gen_ = 0;
}

void MatteCaptureWorker::releaseDevice() {
    capture_.reset();
    blit_.reset();
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
    blit_ = std::make_unique<D3d11Passthrough>(device_);
    capture_ = std::make_unique<D3d11SharedTexture>(device_, context_);
    pipeline_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
    removerLog("MatteCaptureWorker D3D device ready (separate from PIXERA render device)");
    return blit_->ready();
}

bool MatteCaptureWorker::openVideoInLocked() {
    void* handle = nullptr;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    Format fmt = Format::None;
    std::uint64_t gen = 0;
    {
        std::lock_guard<std::mutex> lock(in_mu_);
        handle = video_in_handle_;
        w = in_w_;
        h = in_h_;
        fmt = in_fmt_;
        gen = in_gen_;
    }
    if (!handle || w == 0 || h == 0) {
        return false;
    }
    if (opened_handle_ == handle && opened_gen_ == gen && opened_srv_) {
        return true;
    }
    releaseOpened();

    ID3D11Device1* device1 = nullptr;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&device1))) || !device1) {
        removerLog("MatteCaptureWorker: ID3D11Device1 QI failed");
        return false;
    }
    ID3D11Texture2D* tex = nullptr;
    const HRESULT open_hr =
        device1->OpenSharedResource1(handle, IID_PPV_ARGS(&tex));
    device1->Release();
    if (FAILED(open_hr) || !tex) {
        removerLog(std::format("MatteCaptureWorker OpenSharedResource1 failed hr=0x{:08x}",
                               static_cast<unsigned long>(open_hr)));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = toDxgi(fmt);
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(device_->CreateShaderResourceView(tex, &srv_desc, &srv)) || !srv) {
        tex->Release();
        removerLog("MatteCaptureWorker CreateShaderResourceView failed");
        return false;
    }

    opened_vin_ = tex;
    opened_srv_ = srv;
    opened_handle_ = handle;
    opened_w_ = w;
    opened_h_ = h;
    opened_fmt_ = fmt;
    opened_gen_ = gen;
    capture_->allocate(w, h, Format::R8G8B8A8_UNORM, /*is_target=*/true, /*publish_share=*/false);
    if (pipeline_) {
        pipeline_->resize(w, h);
    }
    removerLog(std::format("MatteCaptureWorker opened VIDEO_IN {}x{} fmt={}", w, h,
                           static_cast<int>(fmt)));
    return true;
}

bool MatteCaptureWorker::captureAndMatteOnce() {
    if (!ensureWorkerDevice() || !openVideoInLocked() || !opened_srv_ || !capture_ || !blit_) {
        return false;
    }

    blit_->draw(context_, opened_srv_, capture_->rtv(), opened_w_, opened_h_);
    // Stall *this* device only until blit finishes, then Map. Never touches PIXERA device.
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
        } else {
            context_->Flush();
        }
    }
    long map_hr = 0;
    if (!capture_->downloadTargetRgba8(scratch_in_, &map_hr)) {
        if ((captures_ % 60u) == 0u) {
            removerLog(std::format("MatteCaptureWorker Map failed hr=0x{:08x}",
                                   static_cast<unsigned long>(map_hr)));
        }
        return false;
    }
    ++captures_;

    bool any = false;
    for (std::uint8_t b : scratch_in_) {
        if (b) {
            any = true;
            break;
        }
    }
    if ((captures_ % 30u) == 1u) {
        D3d11SharedTexture::logFrameStats(scratch_in_, opened_w_, opened_h_, "worker_capture");
    }
    if (!any) {
        if ((captures_ % 60u) == 1u) {
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

    ::genesis::RemoverFrameDebug dbg{};
    std::string error;
    (void)pipeline_->processCpuRgbaFrame(scratch_in_.data(), true, &error, &dbg, false,
                                         &scratch_out_);
    if (!(dbg.stage && std::string_view(dbg.stage) == "matte" && dbg.nonzero_rgb > 0 &&
          !scratch_out_.empty())) {
        if ((captures_ % 60u) == 1u) {
            removerLog(std::format("worker matte not ready stage={} nz={} err={}",
                                   dbg.stage ? dbg.stage : "?", dbg.nonzero_rgb, error));
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
    if ((nonzero_captures_ % 30u) == 1u) {
        removerLog(std::format("worker matte_ok nz={} captures={}/{}", dbg.nonzero_rgb,
                               nonzero_captures_, captures_));
    }
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
        }
    }
    releaseOpened();
    releaseDevice();
    removerLog("MatteCaptureWorker thread exit");
}

} // namespace rxext::genesis_remover
