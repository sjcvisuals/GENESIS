#include "runtime/RemoverPipeline.hpp"

#include <cstring>
#include <stdexcept>

namespace genesis {

RemoverPipeline::RemoverPipeline(std::uint64_t adapter_luid) {
    VulkanContextConfig config{};
    config.adapter_luid = adapter_luid;
    config.enable_validation = false;
    context_ = std::make_unique<VulkanContext>(config);
    // Do not resize, create video_in, load ONNX, or start async here.
    // PIXERA timeline placement calls initialize() and must get a shared sampler
    // without those heavier steps (see ExtGenesisRemover Input).
}

RemoverPipeline::~RemoverPipeline() {
    async_.stop();
}

void RemoverPipeline::ensureScratch() {
    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    if (scratch_in_.size() != bytes) {
        scratch_in_.assign(bytes, 0);
    }
    if (scratch_out_.size() != bytes) {
        scratch_out_.assign(bytes, 0);
    }
}

void RemoverPipeline::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    if (output_ && width == width_ && height == height_) {
        return;
    }
    width_ = width;
    height_ = height;

    // Output first — this is what PIXERA samples on the timeline.
    ExternalRenderTargetConfig out_cfg{};
    out_cfg.width = width_;
    out_cfg.height = height_;
    output_ = std::make_unique<ExternalRenderTarget>(*context_, out_cfg);

    ensureScratch();
    // Keep existing video_in only if size matches; otherwise drop and recreate lazily.
    if (video_in_ && (video_in_->extent().width != width_ || video_in_->extent().height != height_)) {
        video_in_.reset();
    }

    remover_.resetTemporalState();
    if (async_enabled_) {
        ensureAsyncStarted();
        async_.reset(width_, height_);
    }
}

void RemoverPipeline::ensureVideoIn() {
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("RemoverPipeline::ensureVideoIn: call resize first");
    }
    if (video_in_ && video_in_->extent().width == width_ && video_in_->extent().height == height_) {
        return;
    }
    ExternalSharedImageConfig in_cfg{};
    in_cfg.width = width_;
    in_cfg.height = height_;
    video_in_ = std::make_unique<ExternalSharedImage>(*context_, in_cfg);
}

ExternalSharedImage& RemoverPipeline::videoIn() {
    ensureVideoIn();
    return *video_in_;
}

const ExternalSharedImage& RemoverPipeline::videoIn() const {
    if (!video_in_) {
        throw std::runtime_error("RemoverPipeline::videoIn: not created yet");
    }
    return *video_in_;
}

ExternalRenderTarget& RemoverPipeline::output() {
    if (!output_) {
        throw std::runtime_error("RemoverPipeline::output: call resize first");
    }
    return *output_;
}

const ExternalRenderTarget& RemoverPipeline::output() const {
    if (!output_) {
        throw std::runtime_error("RemoverPipeline::output: call resize first");
    }
    return *output_;
}

void RemoverPipeline::ensureAsyncStarted() {
    if (!async_enabled_) {
        return;
    }
    async_.start();
    async_.setSettings(remover_.settings());
}

void RemoverPipeline::ensureRemoverInitialized() {
    if (remover_initialized_) {
        return;
    }
    remover_.initialize(nullptr);
    remover_initialized_ = true;
}

void RemoverPipeline::setAsyncEnabled(bool enabled) {
    if (async_enabled_ == enabled) {
        return;
    }
    async_enabled_ = enabled;
    if (enabled) {
        ensureAsyncStarted();
        if (width_ > 0 && height_ > 0) {
            async_.reset(width_, height_);
        }
    } else {
        async_.stop();
    }
}

void RemoverPipeline::setSettings(const BgRemoverSettings& settings) {
    remover_.setSettings(settings);
    // Defer ONNX load until first processFrame / async start so timeline init stays light.
    if (remover_initialized_) {
        remover_.initialize(nullptr);
    }
    if (async_enabled_) {
        ensureAsyncStarted();
        async_.setSettings(settings);
    }
}

bool RemoverPipeline::processCpuFrame(const std::uint8_t* in_rgba, std::uint8_t* out_rgba,
                                      std::uint32_t width, std::uint32_t height) {
    ensureRemoverInitialized();
    return remover_.process(in_rgba, out_rgba, width, height);
}

bool RemoverPipeline::processCpuFrameAsync(const std::uint8_t* in_rgba, std::uint8_t* out_rgba,
                                           std::uint32_t width, std::uint32_t height) {
    if (!in_rgba || !out_rgba || width == 0 || height == 0) {
        return false;
    }
    ensureRemoverInitialized();
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
    if (!output_) {
        if (out_error) *out_error = "RemoverPipeline output not sized";
        return false;
    }
    if (!video_in_) {
        // No video connected yet — publish transparent frame so timeline stays alive.
        ensureScratch();
        std::memset(scratch_out_.data(), 0, scratch_out_.size());
        if (!output_->uploadColorRgba8(scratch_out_.data())) {
            if (out_error) *out_error = "output upload failed";
            return false;
        }
        return true;
    }
    if (!video_in_->downloadRgba8(scratch_in_)) {
        if (out_error) *out_error = "video download failed";
        return false;
    }

    ensureRemoverInitialized();

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
