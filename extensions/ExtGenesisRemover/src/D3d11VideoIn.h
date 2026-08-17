#pragma once

#include "WinPrefix.h"

#include "rxext.h"

#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace rxext::genesis_remover {

// PIXERA Video In: shared ID3D11Texture2D published as HandleType::D3D11_IMAGE.
// Must be created on the Device's SharedD3d11 (same device as the timeline fence).
class D3d11VideoIn {
public:
    D3d11VideoIn(ID3D11Device* device, ID3D11DeviceContext* context);
    ~D3d11VideoIn();

    D3d11VideoIn(const D3d11VideoIn&) = delete;
    D3d11VideoIn& operator=(const D3d11VideoIn&) = delete;

    void allocate(std::uint32_t width, std::uint32_t height, Format format);
    bool hasTexture() const { return texture_ != nullptr; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    Format format() const { return format_; }
    void* shareHandle() const { return share_handle_; }

    bool downloadRgba8(std::vector<std::uint8_t>& out_rgba);

private:
    void destroyTexture();

    ID3D11Device* device_ = nullptr;           // not owned
    ID3D11DeviceContext* context_ = nullptr;   // not owned
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    void* share_handle_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    Format format_ = Format::None;
};

} // namespace rxext::genesis_remover
