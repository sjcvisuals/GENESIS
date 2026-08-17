#pragma once

#include "WinPrefix.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Device5;
struct ID3D11DeviceContext4;
struct ID3D11Fence;

namespace rxext::genesis_remover {

// One D3D11 device + timeline fence per StreamDevice (ExtSampleD3D pattern).
// PIXERA writes Video In only when this fence handshake is published.
class SharedD3d11 {
public:
    explicit SharedD3d11(std::uint64_t adapter_luid);
    ~SharedD3d11();

    SharedD3d11(const SharedD3d11&) = delete;
    SharedD3d11& operator=(const SharedD3d11&) = delete;

    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }
    ID3D11DeviceContext4* context4() const { return context4_; }
    void* fenceShareHandle() const { return fence_share_; }
    std::uint64_t& fenceCounter() { return fence_counter_; }

    void wait(std::uint64_t value);
    void signal(std::uint64_t value);

private:
    ID3D11Device* device_ = nullptr;
    ID3D11Device5* device5_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11DeviceContext4* context4_ = nullptr;
    ID3D11Fence* fence_ = nullptr;
    void* fence_share_ = nullptr;
    std::uint64_t fence_counter_ = 0;
};

} // namespace rxext::genesis_remover
