#pragma once

#include "WinPrefix.h"

#include "rxext.h"

#include <cstdint>
#include <memory>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace rxext::genesis_remover {

// PIXERA Video In for D3D hosts: allocate shared ID3D11Texture2D and publish
// HandleType::D3D11_IMAGE (ExtSampleD3D). Vulkan OPAQUE_WIN32 Video In stays black
// because the engine writes through D3D, not into Vulkan opaque NT memory.
class D3d11VideoIn {
public:
    explicit D3d11VideoIn(std::uint64_t adapter_luid);
    ~D3d11VideoIn();

    D3d11VideoIn(const D3d11VideoIn&) = delete;
    D3d11VideoIn& operator=(const D3d11VideoIn&) = delete;

    void allocate(std::uint32_t width, std::uint32_t height, Format format);
    bool hasTexture() const { return texture_ != nullptr; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    Format format() const { return format_; }
    void* shareHandle() const { return share_handle_; }

    // GPU → staging → RGBA8 (handles RGBA8 / BGRA8 / RGBA16F / RGBA32F).
    bool downloadRgba8(std::vector<std::uint8_t>& out_rgba);

private:
    void destroyTexture();

    std::uint64_t adapter_luid_ = 0;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    void* share_handle_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    Format format_ = Format::None;
};

} // namespace rxext::genesis_remover
