#include "WinPrefix.h"
#include "D3d11VideoIn.h"

#include "RemoverLog.h"

#include "d3d11/ComPointer.h"
#include "rxext_util.h"

// Full Windows headers needed for ACL / CreateSharedHandle security descriptor.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <aclapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h> // IDXGIResource1::CreateSharedHandle

#include <cstring>
#include <format>
#include <stdexcept>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "advapi32.lib")

namespace rxext::genesis_remover {
namespace {

// Same Everyone+Admin DACL as ExtSampleD3D getSecurityAttributes.cpp (cross-process NT handle).
const SECURITY_ATTRIBUTES& sharedSecurityAttributes() {
    class Holder {
    public:
        Holder() {
            if (!init()) {
                release();
                throw std::runtime_error("D3d11VideoIn: security attributes init failed");
            }
        }
        ~Holder() { release(); }
        const SECURITY_ATTRIBUTES& get() const { return sa_; }

    private:
        PSID everyone_ = nullptr;
        PSID admin_ = nullptr;
        PACL acl_ = nullptr;
        PSECURITY_DESCRIPTOR sd_ = nullptr;
        SECURITY_ATTRIBUTES sa_{};

        void release() {
            if (everyone_) {
                FreeSid(everyone_);
                everyone_ = nullptr;
            }
            if (admin_) {
                FreeSid(admin_);
                admin_ = nullptr;
            }
            if (acl_) {
                LocalFree(acl_);
                acl_ = nullptr;
            }
            if (sd_) {
                LocalFree(sd_);
                sd_ = nullptr;
            }
        }

        bool init() {
            EXPLICIT_ACCESS ea[2]{};
            SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
            SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
            if (!AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
                                          &everyone_)) {
                return false;
            }
            ea[0].grfAccessPermissions = KEY_ALL_ACCESS;
            ea[0].grfAccessMode = SET_ACCESS;
            ea[0].grfInheritance = NO_INHERITANCE;
            ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
            ea[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
            ea[0].Trustee.ptstrName = reinterpret_cast<LPTSTR>(everyone_);

            if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                          DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_)) {
                release();
                return false;
            }
            ea[1].grfAccessPermissions = KEY_ALL_ACCESS;
            ea[1].grfAccessMode = SET_ACCESS;
            ea[1].grfInheritance = NO_INHERITANCE;
            ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
            ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
            ea[1].Trustee.ptstrName = reinterpret_cast<LPTSTR>(admin_);

            if (SetEntriesInAcl(2, ea, nullptr, &acl_) != ERROR_SUCCESS) {
                release();
                return false;
            }
            sd_ = static_cast<PSECURITY_DESCRIPTOR>(LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH));
            if (!sd_ || !InitializeSecurityDescriptor(sd_, SECURITY_DESCRIPTOR_REVISION) ||
                !SetSecurityDescriptorDacl(sd_, TRUE, acl_, FALSE)) {
                release();
                return false;
            }
            sa_.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa_.lpSecurityDescriptor = sd_;
            sa_.bInheritHandle = FALSE;
            return true;
        }
    };
    static Holder holder;
    return holder.get();
}

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

D3d11VideoIn::D3d11VideoIn(std::uint64_t adapter_luid) : adapter_luid_(adapter_luid) {
    ComPointer<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        throw std::runtime_error("D3d11VideoIn: CreateDXGIFactory1 failed");
    }
    const LUID luid = toLuid(adapter_luid_);
    ComPointer<IDXGIAdapter1> adapter = findAdapter(*factory, luid);
    if (!adapter) {
        throw std::runtime_error("D3d11VideoIn: adapter LUID not found");
    }

    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1,
                                 D3D11_SDK_VERSION, &device, nullptr, &context)) ||
        !device || !context) {
        throw std::runtime_error("D3d11VideoIn: D3D11CreateDevice failed");
    }
    device_ = device;
    context_ = context;
    removerLog(std::format("D3d11VideoIn device ready adapter_luid={:016x}", adapter_luid_));
}

D3d11VideoIn::~D3d11VideoIn() {
    destroyTexture();
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void D3d11VideoIn::destroyTexture() {
    if (share_handle_) {
        CloseHandle(share_handle_);
        share_handle_ = nullptr;
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
}

void D3d11VideoIn::allocate(std::uint32_t width, std::uint32_t height, Format format) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("D3d11VideoIn: invalid extent");
    }
    if (texture_ && width_ == width && height_ == height && format_ == format) {
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
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    // ExtSampleD3D: SHARED | SHARED_NTHANDLE is what finally lets PIXERA write pixels.
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_)) || !texture_) {
        throw std::runtime_error("D3d11VideoIn: CreateTexture2D failed");
    }

    IDXGIResource1* dxgi_res = nullptr;
    if (FAILED(texture_->QueryInterface(IID_PPV_ARGS(&dxgi_res))) || !dxgi_res) {
        destroyTexture();
        throw std::runtime_error("D3d11VideoIn: IDXGIResource1 QI failed");
    }
    const HRESULT share_hr = dxgi_res->CreateSharedHandle(
        &sharedSecurityAttributes(), GENERIC_ALL, nullptr, &share_handle_);
    dxgi_res->Release();
    if (FAILED(share_hr) || !share_handle_) {
        destroyTexture();
        throw std::runtime_error("D3d11VideoIn: CreateSharedHandle failed");
    }

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    if (FAILED(device_->CreateTexture2D(&staging_desc, nullptr, &staging_)) || !staging_) {
        destroyTexture();
        throw std::runtime_error("D3d11VideoIn: staging CreateTexture2D failed");
    }

    width_ = width;
    height_ = height;
    format_ = resolved;
    removerLog(std::format("D3d11VideoIn allocate {}x{} format={} dxgi={}", width_, height_,
                           get_format_name(format_), static_cast<int>(dxgi)));
}

bool D3d11VideoIn::downloadRgba8(std::vector<std::uint8_t>& out_rgba) {
    if (!texture_ || !staging_ || !context_ || width_ == 0 || height_ == 0) {
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

} // namespace rxext::genesis_remover
