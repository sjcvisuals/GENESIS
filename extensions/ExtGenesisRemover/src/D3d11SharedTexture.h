#pragma once

#include "WinPrefix.h"

#include "rxext.h"

#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;

namespace rxext::genesis_remover {

// ExtSampleD3D-style shared texture. Used for both Video In (is_target=false)
// and output sampler (is_target=true). Always published as HandleType::D3D11_IMAGE.
class D3d11SharedTexture {
public:
    D3d11SharedTexture(ID3D11Device* device, ID3D11DeviceContext* context);
    ~D3d11SharedTexture();

    D3d11SharedTexture(const D3d11SharedTexture&) = delete;
    D3d11SharedTexture& operator=(const D3d11SharedTexture&) = delete;

    // is_target=true → BIND_RENDER_TARGET (output sampler). false → Video In.
    void allocate(std::uint32_t width, std::uint32_t height, Format format, bool is_target);
    bool hasTexture() const { return texture_ != nullptr; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    Format format() const { return format_; }
    bool isTarget() const { return is_target_; }
    void* shareHandle() const { return share_handle_; }

    bool downloadRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr = nullptr);
    bool uploadRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);
    void clear(float r, float g, float b, float a);
    // Magenta sentinel so we can tell "PIXERA never wrote" vs "PIXERA wrote black".
    bool fillSentinelMagenta();
    static bool looksLikeSentinel(const std::vector<std::uint8_t>& rgba);

private:
    void destroyTexture();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    void* share_handle_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    Format format_ = Format::None;
    bool is_target_ = false;
};

} // namespace rxext::genesis_remover
