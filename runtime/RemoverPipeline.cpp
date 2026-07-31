#include "runtime/RemoverPipeline.hpp"

#include <cstring>

namespace genesis {

RemoverPipeline::RemoverPipeline(std::uint64_t adapter_luid) {
    VulkanContextConfig config{};
    config.adapter_luid = adapter_luid;
    config.enable_validation = false;
    context_ = std::make_unique<VulkanContext>(config);
    resize(1920, 1080);
    remover_.initialize(nullptr);
    // Do not start the async worker here — PIXERA stream init is sensitive to
    // background threads. Caller enables async after the output sampler is published.
}

RemoverPipeline::~RemoverPipeline() {
    async_.stop();
}

void RemoverPipeline::ensureAsyncStarted() {
    if (!async_enabled_) {
        return;
    }
    async_.start();
    async_.setSettings(remover_.settings());
}

void RemoverPipeline::setAsyncEnabled(bool enabled) {
    if (async_enabled_ == enabled) {
        return;
    }
    async_enabled_ = enabled;
    if (enabled) {
        ensureAsyncStarted();
        async_.reset(width_, height_);
    } else {
        async_.stop();
    }
}

void RemoverPipeline::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    if (video_in_ && output_ && width == width_ && height == height_) {
        return;
    }
    width_ = width;
    height_ = height;

    ExternalSharedImageConfig in_cfg{};
    in_cfg.width = width;
    in_cfg.height = height;
    video_in_ = std::make_unique<ExternalSharedImage>(*context_, in_cfg);

    ExternalRenderTargetConfig out_cfg{};
    out_cfg.width = width;
    out_cfg.height = height;
    output_ = std::make_unique<ExternalRenderTarget>(*context_, out_cfg);

    scratch_in_.assign(static_cast<std::size_t>(width) * height * 4u, 0);
    scratch_out_.assign(scratch_in_.size(), 0);
    remover_.resetTemporalState();
    if (async_enabled_) {
        ensureAsyncStarted();
        async_.reset(width, height);
    }
}

void RemoverPipeline::setSettings(const BgRemoverSettings& settings) {
    remover_.setSettings(settings);
    remover_.initialize(nullptr);
    if (async_enabled_) {
        ensureAsyncStarted();
        async_.setSettings(settings);
    }
}

bool RemoverPipeline::processCpuFrame(const std::uint8_t* in_rgba, std::uint8_t* out_rgba,
                                      std::uint32_t width, std::uint32_t height) {
    return remover_.process(in_rgba, out_rgba, width, height);
}

bool RemoverPipeline::processCpuFrameAsync(const std::uint8_t* in_rgba, std::uint8_t* out_rgba,
                                           std::uint32_t width, std::uint32_t height) {
    if (!in_rgba || !out_rgba || width == 0 || height == 0) {
        return false;
    }
    ensureAsyncStarted();
    async_.trySubmit(in_rgba, width, height);
    if (!async_.copyLatest(out_rgba, width, height)) {
        const std::size_t bytes = static_cast<std::size_t>(width) * height * 4u;
        std::memset(out_rgba, 0, bytes);
    }
    return true;
}

bool RemoverPipeline::uploadOutputRgba8(const std::uint8_t* rgba) {
    return output_ && output_->uploadColorRgba8(rgba);
}

bool RemoverPipeline::processFrame(std::string* out_error) {
    if (!video_in_ || !output_) {
        if (out_error) *out_error = "RemoverPipeline not sized";
        return false;
    }
    if (!video_in_->downloadRgba8(scratch_in_)) {
        if (out_error) *out_error = "video download failed";
        return false;
    }

    if (async_enabled_) {
        ensureAsyncStarted();
        async_.trySubmit(scratch_in_.data(), width_, height_);
        if (!async_.copyLatest(scratch_out_.data(), width_, height_)) {
            // Transparent until the first matte completes — never stall render on ORT.
            std::memset(scratch_out_.data(), 0, scratch_out_.size());
        }
    } else if (!remover_.process(scratch_in_.data(), scratch_out_.data(), width_, height_)) {
        if (out_error) *out_error = "matte process failed";
        return false;
    }

    if (!output_->uploadColorRgba8(scratch_out_.data())) {
        if (out_error) *out_error = "output upload failed";
        return false;
    }
    return true;
}

} // namespace genesis
