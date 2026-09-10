#pragma once

#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"
#include "renderer/vulkan/ExternalRenderTarget.hpp"
#include "renderer/vulkan/ExternalSharedImage.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace genesis {

// Per-frame diagnostics for PIXERA Remover debugging (shared Vulkan path).
struct RemoverFrameDebug {
    const char* stage = "none"; // no_output / waiting_video_in / download_fail /
                                // input_black / passthrough / matte / error
    bool has_video_in = false;
    bool download_ok = false;
    bool matte_copied = false;
    std::size_t nonzero_rgb = 0;
    unsigned mid_r = 0;
    unsigned mid_g = 0;
    unsigned mid_b = 0;
    unsigned mid_a = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Headless Pixera-style remover: shared video in → matte → exportable RGBA out.
// Used by ExtGenesis Remover mode and ExtGenesisRemover.
//
// Lifecycle is staged so PIXERA timeline placement can succeed before ONNX/video_in:
//   1) construct          → VulkanContext only
//   2) resize / ensureOutput → ExternalRenderTarget (sampler publish)
//   3) ensureVideoIn      → optional DXGI video_in (may fail; deferred to update())
//   4) processFrame       → lazy BgRemover / async worker
class RemoverPipeline {
public:
    explicit RemoverPipeline(std::uint64_t adapter_luid = 0);
    ~RemoverPipeline();

    RemoverPipeline(const RemoverPipeline&) = delete;
    RemoverPipeline& operator=(const RemoverPipeline&) = delete;

    // Creates/resizes the exportable output. Does not create video_in or load ONNX.
    void resize(std::uint32_t width, std::uint32_t height);

    // Lazily create shared video_in. Recreates when size OR format changes.
    // format must match the TextureDesc advertised to PIXERA (e.g. R16G16B16A16_SFLOAT).
    void ensureVideoIn(VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    // Drop and recreate video_in at the last ensureVideoIn format.
    void forceRecreateVideoIn();

    void setSettings(const BgRemoverSettings& settings);

    // When true, processFrame never blocks on ORT — skips if busy, reuses last matte.
    // Default is false: PIXERA stream init must not start background ORT threads.
    void setAsyncEnabled(bool enabled);
    bool asyncEnabled() const { return async_enabled_; }

    bool hasOutput() const { return static_cast<bool>(output_); }
    bool hasVideoIn() const { return static_cast<bool>(video_in_); }

    ExternalSharedImage& videoIn();
    const ExternalSharedImage& videoIn() const;
    ExternalRenderTarget& output();
    const ExternalRenderTarget& output() const;
    BgRemover& remover() { return remover_; }
    const BgRemover& remover() const { return remover_; }
    AsyncMatteWorker& asyncWorker() { return async_; }
    const AsyncMatteWorker& asyncWorker() const { return async_; }
    VulkanContext& context() { return *context_; }

    // Download video_in → matte → upload into output color image.
    // When show_diagnostics is true, waiting/black stages use solid colours instead of
    // transparent black so PIXERA shows the stall stage (cyan / blue / orange).
    bool processFrame(std::string* out_error = nullptr, RemoverFrameDebug* debug = nullptr,
                      bool show_diagnostics = true);

    // CPU RGBA8 ingest (D3D11 Video In staging path). rgba=nullptr → waiting_video_in.
    // When out_rgba is non-null, receives the RGBAs that were uploaded / would be shown.
    bool processCpuRgbaFrame(const std::uint8_t* rgba, bool has_input,
                             std::string* out_error = nullptr, RemoverFrameDebug* debug = nullptr,
                             bool show_diagnostics = true,
                             std::vector<std::uint8_t>* out_rgba = nullptr);

    // CPU-only path for smoke tests (no shared-image roundtrip). Sync matte.
    bool processCpuFrame(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                         std::uint32_t height);

    // CPU async path for smoke / timing: trySubmit + copyLatest (transparent if none yet).
    bool processCpuFrameAsync(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                              std::uint32_t height);

    bool uploadOutputRgba8(const std::uint8_t* rgba);

private:
    void ensureAsyncStarted();
    void ensureRemoverInitialized();
    void ensureScratch();
    void fillSolid(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<ExternalSharedImage> video_in_;
    std::unique_ptr<ExternalRenderTarget> output_;
    BgRemover remover_;
    AsyncMatteWorker async_;
    bool async_enabled_ = false;
    bool remover_initialized_ = false;
    std::vector<std::uint8_t> scratch_in_;
    std::vector<std::uint8_t> scratch_out_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    VkFormat video_in_format_ = VK_FORMAT_R8G8B8A8_UNORM;
};

} // namespace genesis
