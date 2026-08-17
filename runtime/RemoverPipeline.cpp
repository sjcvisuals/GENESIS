#include "runtime/RemoverPipeline.hpp"

#include <cstring>
#include <stdexcept>
#include <volk.h>

namespace genesis {
namespace {

std::size_t countNonzeroRgb(const std::uint8_t* rgba, std::size_t bytes) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < bytes; i += 4) {
        if (rgba[i] | rgba[i + 1] | rgba[i + 2]) {
            ++n;
        }
    }
    return n;
}

void sampleMid(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h, RemoverFrameDebug& d) {
    if (!rgba || w == 0 || h == 0) {
        return;
    }
    const std::size_t mid = (static_cast<std::size_t>(w) * h / 2u) * 4u;
    d.mid_r = rgba[mid + 0];
    d.mid_g = rgba[mid + 1];
    d.mid_b = rgba[mid + 2];
    d.mid_a = rgba[mid + 3];
}

} // namespace

RemoverPipeline::RemoverPipeline(std::uint64_t adapter_luid) {
    VulkanContextConfig config{};
    config.adapter_luid = adapter_luid;
    config.enable_validation = false;
    context_ = std::make_unique<VulkanContext>(config);
    // Do not resize, create video_in, load ONNX, or start async here.
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

void RemoverPipeline::fillSolid(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    ensureScratch();
    for (std::size_t i = 0; i + 3 < scratch_out_.size(); i += 4) {
        scratch_out_[i + 0] = r;
        scratch_out_[i + 1] = g;
        scratch_out_[i + 2] = b;
        scratch_out_[i + 3] = 255;
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

    ExternalRenderTargetConfig out_cfg{};
    out_cfg.width = width_;
    out_cfg.height = height_;
    output_ = std::make_unique<ExternalRenderTarget>(*context_, out_cfg);

    ensureScratch();
    if (video_in_ && (video_in_->extent().width != width_ || video_in_->extent().height != height_)) {
        video_in_.reset();
    }

    remover_.resetTemporalState();
    if (async_enabled_) {
        ensureAsyncStarted();
        async_.reset(width_, height_);
    }
}

void RemoverPipeline::ensureVideoIn(VkFormat format) {
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("RemoverPipeline::ensureVideoIn: call resize first");
    }
    if (video_in_ && video_in_->extent().width == width_ &&
        video_in_->extent().height == height_ && video_in_->format() == format) {
        video_in_format_ = format;
        return;
    }
    video_in_format_ = format;
    ExternalSharedImageConfig in_cfg{};
    in_cfg.width = width_;
    in_cfg.height = height_;
    in_cfg.format = format;
    video_in_ = std::make_unique<ExternalSharedImage>(*context_, in_cfg);
}

void RemoverPipeline::forceRecreateVideoIn() {
    video_in_.reset();
    ensureVideoIn(video_in_format_);
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

bool RemoverPipeline::processCpuRgbaFrame(const std::uint8_t* rgba, bool has_input,
                                          std::string* out_error, RemoverFrameDebug* debug,
                                          bool show_diagnostics,
                                          std::vector<std::uint8_t>* out_rgba) {
    RemoverFrameDebug local{};
    RemoverFrameDebug& d = debug ? *debug : local;
    d.width = width_;
    d.height = height_;
    d.has_video_in = has_input;

    auto publish_out = [&]() -> bool {
        if (out_rgba) {
            *out_rgba = scratch_out_;
        }
        if (output_) {
            if (!output_->uploadColorRgba8(scratch_out_.data())) {
                d.stage = "error";
                if (out_error) *out_error = "output upload failed";
                return false;
            }
        } else if (!out_rgba) {
            d.stage = "no_output";
            if (out_error) *out_error = "RemoverPipeline output not sized";
            return false;
        }
        return true;
    };

    if (width_ == 0 || height_ == 0) {
        d.stage = "no_output";
        if (out_error) *out_error = "RemoverPipeline not sized";
        return false;
    }

    if (!has_input || !rgba) {
        d.stage = "waiting_video_in";
        if (show_diagnostics) {
            fillSolid(0, 220, 255);
        } else {
            ensureScratch();
            std::memset(scratch_out_.data(), 0, scratch_out_.size());
        }
        return publish_out();
    }

    ensureScratch();
    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    if (scratch_in_.size() < bytes) {
        d.stage = "error";
        if (out_error) *out_error = "scratch size mismatch";
        return false;
    }
    if (rgba != scratch_in_.data()) {
        std::memcpy(scratch_in_.data(), rgba, bytes);
    }
    d.download_ok = true;
    d.nonzero_rgb = countNonzeroRgb(scratch_in_.data(), scratch_in_.size());
    sampleMid(scratch_in_.data(), width_, height_, d);

    if (d.nonzero_rgb == 0) {
        d.stage = "input_black";
        if (show_diagnostics) {
            fillSolid(255, 140, 0);
            return publish_out();
        }
    }

    ensureRemoverInitialized();

    if (async_enabled_) {
        ensureAsyncStarted();
        async_.trySubmit(scratch_in_.data(), width_, height_);
        if (!async_.copyLatest(scratch_out_.data(), width_, height_)) {
            std::memcpy(scratch_out_.data(), scratch_in_.data(), bytes);
            for (std::size_t i = 3; i < bytes; i += 4) {
                scratch_out_[i] = 255;
            }
            d.stage = "passthrough";
            d.matte_copied = false;
        } else {
            d.stage = "matte";
            d.matte_copied = true;
        }
    } else if (!remover_.process(scratch_in_.data(), scratch_out_.data(), width_, height_)) {
        d.stage = "error";
        if (out_error) *out_error = "matte process failed";
        return false;
    } else {
        d.stage = "matte";
        d.matte_copied = true;
    }

    return publish_out();
}

bool RemoverPipeline::processFrame(std::string* out_error, RemoverFrameDebug* debug,
                                   bool show_diagnostics) {
    RemoverFrameDebug local{};
    RemoverFrameDebug& d = debug ? *debug : local;
    d.width = width_;
    d.height = height_;
    d.has_video_in = static_cast<bool>(video_in_);

    if (!output_) {
        d.stage = "no_output";
        if (out_error) *out_error = "RemoverPipeline output not sized";
        return false;
    }
    if (!video_in_) {
        return processCpuRgbaFrame(nullptr, false, out_error, debug, show_diagnostics);
    }
    if (!video_in_->downloadRgba8(scratch_in_)) {
        d.stage = "download_fail";
        if (show_diagnostics) {
            fillSolid(30, 90, 220); // blue = Vulkan shared read failed
            (void)output_->uploadColorRgba8(scratch_out_.data());
        }
        if (out_error) *out_error = "video download failed";
        return false;
    }
    return processCpuRgbaFrame(scratch_in_.data(), true, out_error, debug, show_diagnostics);
}

} // namespace genesis
