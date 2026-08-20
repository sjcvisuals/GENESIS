#include "WinPrefix.h"
#include "D3d11Passthrough.h"

#include "RemoverLog.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <array>
#include <cstring>
#include <format>

#pragma comment(lib, "d3dcompiler.lib")

namespace rxext::genesis_remover {
namespace {

struct Vertex {
    float x, y, z, w;
    float u, v;
};

const char* kVs = R"(
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(VSIn i) {
  VSOut o;
  o.pos = i.pos;
  o.uv = i.uv;
  return o;
}
)";

const char* kPs = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PSIn i) : SV_Target {
  return tex.Sample(samp, i.uv);
}
)";

const char* kSolidPs = R"(
float4 main() : SV_Target {
  return float4(0.2, 1.0, 0.1, 1.0);
}
)";

ID3DBlob* compile(const char* src, const char* entry, const char* target) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    const HRESULT hr =
        D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, entry, target, 0, 0, &blob,
                   &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "unknown";
        removerLog(std::format("D3d11Passthrough shader compile failed: {}", msg));
        if (err) {
            err->Release();
        }
        return nullptr;
    }
    if (err) {
        err->Release();
    }
    return blob;
}

ID3D11Buffer* makeVb(ID3D11Device* device, const std::array<Vertex, 4>& verts) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(sizeof(verts));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = verts.data();
    ID3D11Buffer* buf = nullptr;
    if (FAILED(device->CreateBuffer(&bd, &init, &buf))) {
        return nullptr;
    }
    return buf;
}

} // namespace

D3d11Passthrough::D3d11Passthrough(ID3D11Device* device) {
    if (!device) {
        return;
    }
    ID3DBlob* vs_blob = compile(kVs, "main", "vs_5_0");
    ID3DBlob* ps_blob = compile(kPs, "main", "ps_5_0");
    ID3DBlob* solid_blob = compile(kSolidPs, "main", "ps_5_0");
    if (!vs_blob || !ps_blob || !solid_blob) {
        if (vs_blob) {
            vs_blob->Release();
        }
        if (ps_blob) {
            ps_blob->Release();
        }
        if (solid_blob) {
            solid_blob->Release();
        }
        return;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    const bool ok =
        SUCCEEDED(device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                                             nullptr, &vs_)) &&
        SUCCEEDED(device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(),
                                            nullptr, &ps_)) &&
        SUCCEEDED(device->CreatePixelShader(solid_blob->GetBufferPointer(),
                                            solid_blob->GetBufferSize(), nullptr, &solid_ps_)) &&
        SUCCEEDED(device->CreateInputLayout(layout, 2, vs_blob->GetBufferPointer(),
                                            vs_blob->GetBufferSize(), &layout_));
    vs_blob->Release();
    ps_blob->Release();
    solid_blob->Release();
    if (!ok || !vs_ || !ps_ || !solid_ps_ || !layout_) {
        return;
    }

    // Flipped: PIXERA shared Video In is top-left origin (GL-like).
    const std::array<Vertex, 4> verts_flip = {{
        {-1.f, -1.f, 0.f, 1.f, 0.f, 0.f},
        {-1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        {1.f, -1.f, 0.f, 1.f, 1.f, 0.f},
        {1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
    }};
    // Unflipped: preserve texture row order for capture -> host Map -> matte upload.
    const std::array<Vertex, 4> verts_nflip = {{
        {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f},
        {-1.f, 1.f, 0.f, 1.f, 0.f, 0.f},
        {1.f, -1.f, 0.f, 1.f, 1.f, 1.f},
        {1.f, 1.f, 0.f, 1.f, 1.f, 0.f},
    }};
    vb_flip_ = makeVb(device, verts_flip);
    vb_nflip_ = makeVb(device, verts_nflip);
    if (!vb_flip_ || !vb_nflip_) {
        return;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sd, &sampler_)) || !sampler_) {
        return;
    }

    ready_ = true;
    removerLog("D3d11Passthrough ready (GPU blit + heartbeat, flip/nflip VBs)");
}

D3d11Passthrough::~D3d11Passthrough() {
    if (sampler_) {
        sampler_->Release();
        sampler_ = nullptr;
    }
    if (vb_nflip_) {
        vb_nflip_->Release();
        vb_nflip_ = nullptr;
    }
    if (vb_flip_) {
        vb_flip_->Release();
        vb_flip_ = nullptr;
    }
    if (layout_) {
        layout_->Release();
        layout_ = nullptr;
    }
    if (solid_ps_) {
        solid_ps_->Release();
        solid_ps_ = nullptr;
    }
    if (ps_) {
        ps_->Release();
        ps_ = nullptr;
    }
    if (vs_) {
        vs_->Release();
        vs_ = nullptr;
    }
}

void D3d11Passthrough::draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv,
                            ID3D11RenderTargetView* rtv, std::uint32_t width, std::uint32_t height,
                            bool flip_v) {
    if (!ready_ || !context || !srv || !rtv || width == 0 || height == 0) {
        return;
    }
    ID3D11Buffer* vb = flip_v ? vb_flip_ : vb_nflip_;
    if (!vb) {
        return;
    }

    context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->IASetInputLayout(layout_);
    context->VSSetShader(vs_, nullptr, 0);
    context->PSSetShader(ps_, nullptr, 0);
    context->PSSetShaderResources(0, 1, &srv);
    context->PSSetSamplers(0, 1, &sampler_);
    context->Draw(4, 0);

    ID3D11ShaderResourceView* null_srv = nullptr;
    context->PSSetShaderResources(0, 1, &null_srv);
    ID3D11RenderTargetView* null_rtv = nullptr;
    context->OMSetRenderTargets(0, &null_rtv, nullptr);
}

void D3d11Passthrough::drawHeartbeat(ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv,
                                     std::uint32_t width, std::uint32_t height,
                                     std::uint64_t tick) {
    if (!ready_ || !context || !rtv || !solid_ps_ || !vb_flip_ || width == 0 || height < 16) {
        return;
    }
    constexpr float kBarH = 14.0f;
    const float y = static_cast<float>((tick * 3u) % static_cast<std::uint64_t>(height - 14u));

    context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = y;
    vp.Width = static_cast<float>(width);
    vp.Height = kBarH;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vb_flip_, &stride, &offset);
    context->IASetInputLayout(layout_);
    context->VSSetShader(vs_, nullptr, 0);
    context->PSSetShader(solid_ps_, nullptr, 0);
    ID3D11ShaderResourceView* null_srv = nullptr;
    context->PSSetShaderResources(0, 1, &null_srv);
    context->Draw(4, 0);

    ID3D11RenderTargetView* null_rtv = nullptr;
    context->OMSetRenderTargets(0, &null_rtv, nullptr);
}

} // namespace rxext::genesis_remover
