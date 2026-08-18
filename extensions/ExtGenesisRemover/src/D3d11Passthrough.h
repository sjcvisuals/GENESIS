#pragma once

#include "WinPrefix.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;

namespace rxext::genesis_remover {

// ExtSampleD3D-style fullscreen blit: sample Video In SRV into output RTV.
class D3d11Passthrough {
public:
    explicit D3d11Passthrough(ID3D11Device* device);
    ~D3d11Passthrough();

    D3d11Passthrough(const D3d11Passthrough&) = delete;
    D3d11Passthrough& operator=(const D3d11Passthrough&) = delete;

    bool ready() const { return ready_; }
    void draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv,
              ID3D11RenderTargetView* rtv, std::uint32_t width, std::uint32_t height);
    // Moving lime stripe so we can tell if shared output is updating (vs stale compositor).
    void drawHeartbeat(ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv,
                       std::uint32_t width, std::uint32_t height, std::uint64_t tick);

private:
    bool ready_ = false;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11PixelShader* solid_ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

} // namespace rxext::genesis_remover
