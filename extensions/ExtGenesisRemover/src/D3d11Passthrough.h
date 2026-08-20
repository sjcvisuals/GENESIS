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
    // flip_v=true: PIXERA Video In is top-left origin; flip for correct preview.
    // flip_v=false: keep texture orientation (use for capture share so host Map
    // matches Video In; matte then displays with flip_v=true once).
    void draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv,
              ID3D11RenderTargetView* rtv, std::uint32_t width, std::uint32_t height,
              bool flip_v = true);
    // Live camera at full rate; matte alpha (possibly lower-res / stale) gates RGB.
    // Fixes perceived lag when RVM runs ~15fps but PIXERA presents ~60fps.
    void drawLiveMasked(ID3D11DeviceContext* context, ID3D11ShaderResourceView* live_srv,
                        ID3D11ShaderResourceView* matte_srv, ID3D11RenderTargetView* rtv,
                        std::uint32_t width, std::uint32_t height, bool flip_v = true);
    // Moving lime stripe so we can tell if shared output is updating (vs stale compositor).
    void drawHeartbeat(ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv,
                       std::uint32_t width, std::uint32_t height, std::uint64_t tick);

private:
    bool ready_ = false;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11PixelShader* mask_ps_ = nullptr;
    ID3D11PixelShader* solid_ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_flip_ = nullptr;
    ID3D11Buffer* vb_nflip_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

} // namespace rxext::genesis_remover
