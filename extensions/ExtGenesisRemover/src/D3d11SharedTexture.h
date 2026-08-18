#pragma once

#include "WinPrefix.h"

#include "rxext.h"

#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace rxext::genesis_remover {

// ExtSampleD3D-style shared texture. Used for both Video In (is_target=false)
// and output sampler (is_target=true). Always published as HandleType::D3D11_IMAGE.
class D3d11SharedTexture {
public:
    D3d11SharedTexture(ID3D11Device* device, ID3D11DeviceContext* context);
    ~D3d11SharedTexture();

    D3d11SharedTexture(const D3d11SharedTexture&) = delete;
    D3d11SharedTexture& operator=(const D3d11SharedTexture&) = delete;

    // is_target=true → BIND_RENDER_TARGET. publish_share=false → private GPU RT (no NT handle);
    // used to Map for matte without CopyResource on the shared output PIXERA is sampling.
    void allocate(std::uint32_t width, std::uint32_t height, Format format, bool is_target,
                  bool publish_share = true);
    bool hasTexture() const { return texture_ != nullptr; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    Format format() const { return format_; }
    bool isTarget() const { return is_target_; }
    void* shareHandle() const { return share_handle_; }
    ID3D11ShaderResourceView* srv() const { return srv_; }
    ID3D11RenderTargetView* rtv() const { return rtv_; }

    // Video In (is_target=false) readback.
    bool downloadRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr = nullptr);
    // After GPU blit into output (is_target=true), read RGBA8 for matte.
    bool downloadTargetRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr = nullptr);
    bool uploadRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);
    void clear(float r, float g, float b, float a);
    // Magenta sentinel so we can tell "PIXERA never wrote" vs "PIXERA wrote black".
    // FP16 uses a 1x1 mid-pixel UpdateSubresource (full-frame upload can stall PIXERA).
    bool fillSentinelMagenta();
    static bool looksLikeSentinel(const std::vector<std::uint8_t>& rgba);
    // Raw mid-pixel diagnostics (FP16 bits or RGBA8 bytes) after a successful download path.
    static void logMidRaw(const std::vector<std::uint8_t>& rgba, Format format, std::uint32_t width,
                          std::uint32_t height, const char* tag);
    // max_rgb + corner samples — mid can be letterbox black while feed has pixels.
    static void logFrameStats(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                              std::uint32_t height, const char* tag);

private:
    void destroyTexture();
    bool mapStagingToRgba8(std::vector<std::uint8_t>& out_rgba, long* out_hr);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    void* share_handle_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    Format format_ = Format::None;
    bool is_target_ = false;
    bool publish_share_ = true;
};

} // namespace rxext::genesis_remover
