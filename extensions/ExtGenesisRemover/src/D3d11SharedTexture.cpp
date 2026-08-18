#include "WinPrefix.h"
#include "D3d11SharedTexture.h"

#include "RemoverLog.h"
#include "SharedSecurity.h"
#include "rxext_util.h"

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

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
    // Match ExtSampleD3D TextureDesc: Video In = SRV only; sampler/output = SRV|RTV.
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (is_target) {
        desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    // ExtSampleD3D: SHARED | SHARED_NTHANDLE (keyed mutex → black).
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_)) || !texture_) {
        // Fallback: SRV-only (strict ExtSampleD3D) if RTV+share rejects the format.
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (is_target) {
            desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        }
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_)) || !texture_) {
            throw std::runtime_error("D3d11SharedTexture: CreateTexture2D failed");
        }
        removerLog("D3d11SharedTexture: fell back to ExtSampleD3D bind flags");
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

    // RTV whenever BIND_RENDER_TARGET is set (input sentinel clear + PIXERA blit + output).
    {
        D3D11_TEXTURE2D_DESC created{};
        texture_->GetDesc(&created);
        if (created.BindFlags & D3D11_BIND_RENDER_TARGET) {
            if (FAILED(device_->CreateRenderTargetView(texture_, nullptr, &rtv_)) || !rtv_) {
                destroyTexture();
                throw std::runtime_error("D3d11SharedTexture: CreateRenderTargetView failed");
            }
        }
    }

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = is_target ? D3D11_CPU_ACCESS_WRITE : D3D11_CPU_ACCESS_READ;
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
    if (rtv_ && format_ == Format::R8G8B8A8_UNORM) {
        clear(1.0f, 0.0f, 1.0f, 1.0f);
        context_->Flush();
        return true;
    }
    if (rtv_ && format_ == Format::R16G16B16A16_SFLOAT) {
        clear(1.0f, 0.0f, 1.0f, 1.0f);
        context_->Flush();
        return true;
    }
    // UpdateSubresource fallback for SRV-only textures (Video In has no RTV).
    if (format_ == Format::R8G8B8A8_UNORM) {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(width_) * height_ * 4u);
        for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
            px[i + 0] = 255;
            px[i + 1] = 0;
            px[i + 2] = 255;
            px[i + 3] = 255;
        }
        context_->UpdateSubresource(texture_, 0, nullptr, px.data(), width_ * 4u, 0);
        context_->Flush();
        return true;
    }
    if (format_ == Format::R16G16B16A16_SFLOAT) {
        // IEEE float16: 1.0 = 0x3C00, 0.0 = 0x0000 → magenta (1,0,1,1).
        std::vector<std::uint16_t> px(static_cast<std::size_t>(width_) * height_ * 4u);
        for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
            px[i + 0] = 0x3C00;
            px[i + 1] = 0x0000;
            px[i + 2] = 0x3C00;
            px[i + 3] = 0x3C00;
        }
        context_->UpdateSubresource(texture_, 0, nullptr, px.data(), width_ * 8u, 0);
        context_->Flush();
        return true;
    }
    return false;
}

bool D3d11SharedTexture::looksLikeSentinel(const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() < 4) {
        return false;
    }
    // Mid-pixel roughly magenta (allow small conversion error for FP16).
    const std::size_t mid = (rgba.size() / 8u) * 4u;
    const unsigned r = rgba[mid + 0];
    const unsigned g = rgba[mid + 1];
    const unsigned b = rgba[mid + 2];
    return r >= 200 && g <= 40 && b >= 200;
}

bool D3d11SharedTexture::downloadRgba8(std::vector<std::uint8_t>& out_rgba) {
    if (!texture_ || !staging_ || !context_ || width_ == 0 || height_ == 0 || is_target_) {
        return false;
    }
    context_->CopyResource(staging_, texture_);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData) {
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
        return false;
    }

    context_->Unmap(staging_, 0);
    return true;
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
