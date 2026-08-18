#include "WinPrefix.h"
#include "SharedD3d11.h"
#include "SharedSecurity.h"

#include "RemoverLog.h"

#include "d3d11/ComPointer.h"

#include <aclapi.h>
#include <d3d11_4.h>
#include <dxgi.h>

#include <format>
#include <stdexcept>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "advapi32.lib")

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

const SECURITY_ATTRIBUTES& sharedSecurityAttributes() {
    class Holder {
    public:
        Holder() {
            if (!init()) {
                release();
                throw std::runtime_error("sharedSecurityAttributes init failed");
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
                return false;
            }
            ea[1].grfAccessPermissions = KEY_ALL_ACCESS;
            ea[1].grfAccessMode = SET_ACCESS;
            ea[1].grfInheritance = NO_INHERITANCE;
            ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
            ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
            ea[1].Trustee.ptstrName = reinterpret_cast<LPTSTR>(admin_);

            if (SetEntriesInAcl(2, ea, nullptr, &acl_) != ERROR_SUCCESS) {
                return false;
            }
            sd_ = static_cast<PSECURITY_DESCRIPTOR>(LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH));
            if (!sd_ || !InitializeSecurityDescriptor(sd_, SECURITY_DESCRIPTOR_REVISION) ||
                !SetSecurityDescriptorDacl(sd_, TRUE, acl_, FALSE)) {
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

SharedD3d11::SharedD3d11(std::uint64_t adapter_luid) {
    ComPointer<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        throw std::runtime_error("SharedD3d11: CreateDXGIFactory1 failed");
    }
    ComPointer<IDXGIAdapter1> adapter = findAdapter(*factory, toLuid(adapter_luid));
    if (!adapter) {
        throw std::runtime_error("SharedD3d11: adapter LUID not found");
    }

    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1,
                                 D3D11_SDK_VERSION, &device_, nullptr, &context_)) ||
        !device_ || !context_) {
        throw std::runtime_error("SharedD3d11: D3D11CreateDevice failed");
    }

    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&device5_))) || !device5_) {
        throw std::runtime_error("SharedD3d11: ID3D11Device5 QI failed");
    }
    if (FAILED(context_->QueryInterface(IID_PPV_ARGS(&context4_))) || !context4_) {
        throw std::runtime_error("SharedD3d11: ID3D11DeviceContext4 QI failed");
    }

    if (FAILED(device5_->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence_))) || !fence_) {
        throw std::runtime_error("SharedD3d11: CreateFence failed");
    }
    if (FAILED(fence_->CreateSharedHandle(&sharedSecurityAttributes(), GENERIC_ALL, nullptr,
                                          &fence_share_)) ||
        !fence_share_) {
        throw std::runtime_error("SharedD3d11: fence CreateSharedHandle failed");
    }

    removerLog(std::format("SharedD3d11 ready adapter_luid={:016x} (device+fence)", adapter_luid));
}

SharedD3d11::~SharedD3d11() {
    if (fence_share_) {
        CloseHandle(fence_share_);
        fence_share_ = nullptr;
    }
    if (fence_) {
        fence_->Release();
        fence_ = nullptr;
    }
    if (context4_) {
        context4_->Release();
        context4_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device5_) {
        device5_->Release();
        device5_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void SharedD3d11::wait(std::uint64_t value) {
    if (context4_ && fence_) {
        context4_->Wait(fence_, value);
    }
}

void SharedD3d11::signal(std::uint64_t value) {
    if (context4_ && fence_) {
        context4_->Signal(fence_, value);
    }
}

std::uint64_t SharedD3d11::completedValue() const {
    return fence_ ? fence_->GetCompletedValue() : 0;
}

bool SharedD3d11::waitCpu(std::uint64_t value, std::uint32_t timeout_ms) {
    if (!fence_) {
        return false;
    }
    if (fence_->GetCompletedValue() >= value) {
        return true;
    }
    HANDLE event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!event) {
        return false;
    }
    const HRESULT hr = fence_->SetEventOnCompletion(value, event);
    if (FAILED(hr)) {
        CloseHandle(event);
        return false;
    }
    const DWORD wait = WaitForSingleObject(event, timeout_ms);
    CloseHandle(event);
    return wait == WAIT_OBJECT_0;
}

} // namespace rxext::genesis_remover
