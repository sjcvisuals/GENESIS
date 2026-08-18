#include "WinPrefix.h"
#include "D3d11SharedTexture.h"

#include "RemoverLog.h"
#include "SharedSecurity.h"
#include "rxext_util.h"

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>
#include <vector>

namespace rxext::genesis_remover {
namespace {

DXGI_FORMAT toDxgi(Format format) {
    switch (format) {
    case Format::R8_UNORM:
        return DXGI_FORMAT_R8_UNORM;
    case Format::R8G8_UNORM:
        return DXGI_FORMAT_R8G8_UNORM;
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

std::uint8_t clampU8(float v) {
    if (v <= 0.0f) {
        return 0;
    }
    if (v >= 1.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u;
    const std::uint32_t exp = (h >> 10) & 0x1fu;
    const std::uint32_t mant = h & 0x3ffu;
    std::uint32_t fbits = 0;
    if (exp == 0) {
        if (mant == 0) {
            fbits = sign << 31;
        } else {
            std::uint32_t m = mant;
            std::uint32_t e = 127 - 15 + 1;
            while ((m & 0x400u) == 0u) {
                m <<= 1;
                --e;
            }
            m &= 0x3ffu;
            fbits = (sign << 31) | (e << 23) | (m << 13);
        }
    } else if (exp == 31) {
        fbits = (sign << 31) | 0x7f800000u | (mant << 13);
    } else {
        fbits = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &fbits, sizeof(out));
    if (out != out || out > 1e10f || out < -1e10f) {
        return 0.0f;
    }
    return out;
}

} // namespace

D3d11SharedTexture::D3d11SharedTexture(ID3D11Device* device, ID3D11DeviceContext* context)
    : device_(device), context_(context) {
    if (!device_ || !context_) {
        throw std::runtime_error("D3d11SharedTexture: null device/context");
    }
}

D3d11SharedTexture::~D3d11SharedTexture() {
    destroyTexture();
}

void D3d11SharedTexture::destroyTexture() {
    if (share_handle_) {
        CloseHandle(share_handle_);
        share_handle_ = nullptr;
    }
    if (srv_) {
        srv_->Release();
        srv_ = nullptr;
    }
    if (rtv_) {
        rtv_->Release();
        rtv_ = nullptr;
    }
    if (staging_) {
        staging_->Release();
        staging_ = nullptr;
    }
    if (texture_) {
        texture_->Release();
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
    format_ = Format::None;
    is_target_ = false;
}

void D3d11SharedTexture::allocate(std::uint32_t width, std::uint32_t height, Format format,
                                  bool is_target) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("D3d11SharedTexture: invalid extent");
    }
    if (texture_ && width_ == width && height_ == height && format_ == format &&
        is_target_ == is_target) {
        return;
    }
    destroyTexture();

    const Format resolved = format == Format::None ? Format::R8G8B8A8_UNORM : format;
    const DXGI_FORMAT dxgi = toDxgi(resolved);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = dxgi;
    desc.SampleDesc = {1, 0};
    desc.Usage = D3D11_USAGE_DEFAULT;
    // Match ExtSampleD3D exactly: Video In = SRV only; output = SRV|RTV.
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (is_target) {
        desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    // ExtSampleD3D: SHARED | SHARED_NTHANDLE (keyed mutex → black).
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_)) || !texture_) {
        throw std::runtime_error("D3d11SharedTexture: CreateTexture2D failed");
    }

    IDXGIResource1* dxgi_res = nullptr;
    if (FAILED(texture_->QueryInterface(IID_PPV_ARGS(&dxgi_res))) || !dxgi_res) {
        destroyTexture();
        throw std::runtime_error("D3d11SharedTexture: IDXGIResource1 QI failed");
    }
    const HRESULT share_hr = dxgi_res->CreateSharedHandle(
        &sharedSecurityAttributes(), GENERIC_ALL, nullptr, &share_handle_);
    dxgi_res->Release();
    if (FAILED(share_hr) || !share_handle_) {
        destroyTexture();
        throw std::runtime_error("D3d11SharedTexture: CreateSharedHandle failed");
    }

    // RTV whenever BIND_RENDER_TARGET is set (output sampler).
    {
        D3D11_TEXTURE2D_DESC created{};
        texture_->GetDesc(&created);
        if (created.BindFlags & D3D11_BIND_RENDER_TARGET) {
            if (FAILED(device_->CreateRenderTargetView(texture_, nullptr, &rtv_)) || !rtv_) {
                destroyTexture();
                throw std::runtime_error("D3d11SharedTexture: CreateRenderTargetView failed");
            }
        }
        // SRV for Video In sampling (ExtSampleD3D Quad) and optional output debug.
        if (created.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.Format = created.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            if (FAILED(device_->CreateShaderResourceView(texture_, &srv_desc, &srv_)) || !srv_) {
                destroyTexture();
                throw std::runtime_error("D3d11SharedTexture: CreateShaderResourceView failed");
            }
        }
    }

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    // READ|WRITE so output can upload and also downloadTarget after GPU blit.
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    staging_desc.MiscFlags = 0;
    if (FAILED(device_->CreateTexture2D(&staging_desc, nullptr, &staging_)) || !staging_) {
        destroyTexture();
        throw std::runtime_error("D3d11SharedTexture: staging CreateTexture2D failed");
    }

    width_ = width;
    height_ = height;
    format_ = resolved;
    is_target_ = is_target;
    removerLog(std::format("D3d11SharedTexture allocate {}x{} format={} dxgi={} target={}", width_,
                           height_, get_format_name(format_), static_cast<int>(dxgi),
                           is_target_ ? 1 : 0));
}

void D3d11SharedTexture::clear(float r, float g, float b, float a) {
    if (!rtv_ || !context_) {
        return;
    }
    const float color[4] = {r, g, b, a};
    context_->ClearRenderTargetView(rtv_, color);
}

bool D3d11SharedTexture::fillSentinelMagenta() {
    if (!texture_ || !context_ || width_ == 0 || height_ == 0) {
        return false;
    }
    // Prefer RTV Clear (no Flush — Flush before PIXERA open hung v25).
    if (rtv_) {
        clear(1.0f, 0.0f, 1.0f, 1.0f);
        return true;
    }
    return false;
}

bool D3d11SharedTexture::looksLikeSentinel(const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() < 4) {
        return false;
    }
    const std::size_t mid = (rgba.size() / 8u) * 4u;
    const unsigned r = rgba[mid + 0];
    const unsigned g = rgba[mid + 1];
    const unsigned b = rgba[mid + 2];
    return r >= 200 && g <= 40 && b >= 200;
}

void D3d11SharedTexture::logMidRaw(const std::vector<std::uint8_t>& rgba, Format format,
                                   std::uint32_t width, std::uint32_t height, const char* tag) {
    if (rgba.size() < 4 || width == 0 || height == 0) {
        removerLog(std::format("{} mid_raw: empty", tag ? tag : "?"));
        return;
    }
    const std::size_t mid = (static_cast<std::size_t>(height / 2u) * width + (width / 2u)) * 4u;
    if (mid + 3 >= rgba.size()) {
        return;
    }
    std::size_t nz = 0;
    for (unsigned char b : rgba) {
        if (b != 0) {
            ++nz;
        }
    }
    removerLog(std::format("{} mid_rgba=({},{},{},{}) nz_bytes={} fmt={} {}x{}", tag ? tag : "?",
                           rgba[mid], rgba[mid + 1], rgba[mid + 2], rgba[mid + 3], nz,
                           get_format_name(format), width, height));
}

void D3d11SharedTexture::logFrameStats(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                                       std::uint32_t height, const char* tag) {
    if (rgba.size() < 4 || width == 0 || height == 0) {
        return;
    }
    auto at = [&](std::uint32_t x, std::uint32_t y) -> const std::uint8_t* {
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
        return rgba.data() + i;
    };
    unsigned max_rgb = 0;
    std::size_t nz = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const unsigned m = std::max({rgba[i], rgba[i + 1], rgba[i + 2]});
        if (m > max_rgb) {
            max_rgb = m;
        }
        if (rgba[i] | rgba[i + 1] | rgba[i + 2]) {
            ++nz;
        }
    }
    const auto* tl = at(0, 0);
    const auto* tr = at(width - 1, 0);
    const auto* bl = at(0, height - 1);
    const auto* br = at(width - 1, height - 1);
    const auto* mid = at(width / 2u, height / 2u);
    removerLog(std::format(
        "{} stats max_rgb={} nz={} mid=({},{},{},{}) tl=({},{},{}) tr=({},{},{}) "
        "bl=({},{},{}) br=({},{},{})",
        tag ? tag : "?", max_rgb, nz, mid[0], mid[1], mid[2], mid[3], tl[0], tl[1], tl[2], tr[0],
        tr[1], tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2]));
}

bool D3d11SharedTexture::mapStagingToRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map_hr = context_->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped);
    if (out_hr) {
        *out_hr = static_cast<long>(map_hr);
    }
    if (FAILED(map_hr) || !mapped.pData) {
        return false;
    }

    const std::size_t pixels = static_cast<std::size_t>(width_) * height_;
    out_rgba.resize(pixels * 4u);
    const auto* base = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t row_pitch = mapped.RowPitch;

    if (format_ == Format::R8G8B8A8_UNORM) {
        for (std::uint32_t y = 0; y < height_; ++y) {
            std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * width_ * 4u,
                        base + static_cast<std::size_t>(y) * row_pitch, width_ * 4u);
        }
    } else if (format_ == Format::B8G8R8A8_UNORM) {
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto* src = base + static_cast<std::size_t>(y) * row_pitch;
            auto* dst = out_rgba.data() + static_cast<std::size_t>(y) * width_ * 4u;
            for (std::uint32_t x = 0; x < width_; ++x) {
                dst[x * 4u + 0] = src[x * 4u + 2];
                dst[x * 4u + 1] = src[x * 4u + 1];
                dst[x * 4u + 2] = src[x * 4u + 0];
                dst[x * 4u + 3] = src[x * 4u + 3];
            }
        }
    } else if (format_ == Format::R16G16B16A16_SFLOAT) {
        const std::uint32_t mx = width_ / 2u;
        const std::uint32_t my = height_ / 2u;
        const auto* mid_src = reinterpret_cast<const std::uint16_t*>(
            base + static_cast<std::size_t>(my) * row_pitch + static_cast<std::size_t>(mx) * 8u);
        const std::uint16_t raw0 = mid_src[0];
        const std::uint16_t raw1 = mid_src[1];
        const std::uint16_t raw2 = mid_src[2];
        const std::uint16_t raw3 = mid_src[3];
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto* src =
                reinterpret_cast<const std::uint16_t*>(base + static_cast<std::size_t>(y) * row_pitch);
            auto* dst = out_rgba.data() + static_cast<std::size_t>(y) * width_ * 4u;
            for (std::uint32_t x = 0; x < width_; ++x) {
                dst[x * 4u + 0] = clampU8(halfToFloat(src[x * 4u + 0]));
                dst[x * 4u + 1] = clampU8(halfToFloat(src[x * 4u + 1]));
                dst[x * 4u + 2] = clampU8(halfToFloat(src[x * 4u + 2]));
                dst[x * 4u + 3] = clampU8(halfToFloat(src[x * 4u + 3]));
            }
        }
        bool any = false;
        for (unsigned char b : out_rgba) {
            if (b) {
                any = true;
                break;
            }
        }
        if (!any) {
            static unsigned zero_logs = 0;
            if ((zero_logs++ % 30u) == 0u) {
                removerLog(std::format(
                    "FP16 raw mid=({:04x},{:04x},{:04x},{:04x}) converted all zero", raw0, raw1,
                    raw2, raw3));
            }
        }
    } else if (format_ == Format::R32G32B32A32_SFLOAT) {
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto* src =
                reinterpret_cast<const float*>(base + static_cast<std::size_t>(y) * row_pitch);
            auto* dst = out_rgba.data() + static_cast<std::size_t>(y) * width_ * 4u;
            for (std::uint32_t x = 0; x < width_; ++x) {
                dst[x * 4u + 0] = clampU8(src[x * 4u + 0]);
                dst[x * 4u + 1] = clampU8(src[x * 4u + 1]);
                dst[x * 4u + 2] = clampU8(src[x * 4u + 2]);
                dst[x * 4u + 3] = clampU8(src[x * 4u + 3]);
            }
        }
    } else {
        context_->Unmap(staging_, 0);
        if (out_hr) {
            *out_hr = E_NOTIMPL;
        }
        return false;
    }

    context_->Unmap(staging_, 0);
    return true;
}

bool D3d11SharedTexture::downloadRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr) {
    if (out_hr) {
        *out_hr = S_OK;
    }
    if (!texture_ || !staging_ || !context_ || width_ == 0 || height_ == 0 || is_target_) {
        if (out_hr) {
            *out_hr = E_INVALIDARG;
        }
        return false;
    }
    context_->CopyResource(staging_, texture_);
    return mapStagingToRgba8(out_rgba, out_hr);
}

bool D3d11SharedTexture::downloadTargetRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr) {
    if (out_hr) {
        *out_hr = S_OK;
    }
    if (!texture_ || !staging_ || !context_ || width_ == 0 || height_ == 0 || !is_target_ ||
        format_ != Format::R8G8B8A8_UNORM) {
        if (out_hr) {
            *out_hr = E_INVALIDARG;
        }
        return false;
    }
    context_->CopyResource(staging_, texture_);
    return mapStagingToRgba8(out_rgba, out_hr);
}

bool D3d11SharedTexture::uploadRgba8(const std::uint8_t* rgba, std::uint32_t width,
                                     std::uint32_t height) {
    if (!rgba || !texture_ || !staging_ || !context_ || !is_target_ || width != width_ ||
        height != height_ || format_ != Format::R8G8B8A8_UNORM) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging_, 0, D3D11_MAP_WRITE, 0, &mapped)) || !mapped.pData) {
        return false;
    }
    auto* dst = static_cast<std::uint8_t*>(mapped.pData);
    for (std::uint32_t y = 0; y < height_; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * mapped.RowPitch,
                    rgba + static_cast<std::size_t>(y) * width_ * 4u, width_ * 4u);
    }
    context_->Unmap(staging_, 0);
    context_->CopyResource(texture_, staging_);
    return true;
}

} // namespace rxext::genesis_remover
