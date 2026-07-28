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

// Headless Pixera-style remover: shared video in → matte → exportable RGBA out.
// Used by ExtGenesis Remover mode and ExtGenesisRemover.
class RemoverPipeline {
public:
    explicit RemoverPipeline(std::uint64_t adapter_luid = 0);
    ~RemoverPipeline();

    RemoverPipeline(const RemoverPipeline&) = delete;
    RemoverPipeline& operator=(const RemoverPipeline&) = delete;

    void resize(std::uint32_t width, std::uint32_t height);
    void setSettings(const BgRemoverSettings& settings);

    // When true (default), processFrame never blocks on ORT — skips if busy, reuses last matte.
    void setAsyncEnabled(bool enabled);
    bool asyncEnabled() const { return async_enabled_; }

    ExternalSharedImage& videoIn() { return *video_in_; }
    const ExternalSharedImage& videoIn() const { return *video_in_; }
    ExternalRenderTarget& output() { return *output_; }
    const ExternalRenderTarget& output() const { return *output_; }
    BgRemover& remover() { return remover_; }
    const BgRemover& remover() const { return remover_; }
    AsyncMatteWorker& asyncWorker() { return async_; }
    const AsyncMatteWorker& asyncWorker() const { return async_; }
    VulkanContext& context() { return *context_; }

    // Download video_in → matte → upload into output color image.
    bool processFrame(std::string* out_error = nullptr);

    // CPU-only path for smoke tests (no shared-image roundtrip). Sync matte.
    bool processCpuFrame(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                         std::uint32_t height);

    // CPU async path for smoke / timing: trySubmit + copyLatest (transparent if none yet).
    bool processCpuFrameAsync(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                              std::uint32_t height);

    bool uploadOutputRgba8(const std::uint8_t* rgba);

private:
    void ensureAsyncStarted();

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<ExternalSharedImage> video_in_;
    std::unique_ptr<ExternalRenderTarget> output_;
    BgRemover remover_;
    AsyncMatteWorker async_;
    bool async_enabled_ = true;
    std::vector<std::uint8_t> scratch_in_;
    std::vector<std::uint8_t> scratch_out_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace genesis
