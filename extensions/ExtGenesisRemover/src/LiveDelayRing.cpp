#include "WinPrefix.h"
#include "LiveDelayRing.h"

#include "D3d11Passthrough.h"
#include "rxext.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rxext::genesis_remover {

void LiveDelayRing::reset() {
    slots_.clear();
    write_ = 0;
    filled_ = 0;
    width_ = 0;
    height_ = 0;
    delay_ms_ = 0.0;
}

std::size_t LiveDelayRing::slotsForDelay(double delay_ms) {
    // Assume ~60 present/sec; keep a couple of spare slots. Cap hard for VRAM.
    const double frames = std::ceil(delay_ms / (1000.0 / 60.0)) + 2.0;
    const auto n = static_cast<std::size_t>(std::clamp(frames, 3.0, static_cast<double>(kMaxFrames)));
    return n;
}

ID3D11ShaderResourceView* LiveDelayRing::pushAndSample(SharedD3d11& d3d, D3d11Passthrough& blit,
                                                       ID3D11ShaderResourceView* live_srv,
                                                       std::uint32_t width, std::uint32_t height,
                                                       double delay_ms) {
    if (!live_srv || width == 0 || height == 0 || !blit.ready()) {
        return live_srv;
    }
    if (delay_ms <= 0.5) {
        return live_srv;
    }

    const std::size_t need = slotsForDelay(delay_ms);
    if (slots_.size() != need || width_ != width || height_ != height ||
        std::abs(delay_ms_ - delay_ms) > 15.0) {
        slots_.clear();
        slots_.resize(need);
        for (std::size_t i = 0; i < need; ++i) {
            slots_[i].tex = std::make_unique<D3d11SharedTexture>(d3d.device(), d3d.context());
            slots_[i].tex->allocate(width, height, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                    /*publish_share=*/false);
            slots_[i].valid = false;
        }
        write_ = 0;
        filled_ = 0;
        width_ = width;
        height_ = height;
        delay_ms_ = delay_ms;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::size_t n = slots_.size();
    blit.draw(d3d.context(), live_srv, slots_[write_].tex->rtv(), width, height, /*flip_v=*/false);
    slots_[write_].stamped = now;
    slots_[write_].valid = true;
    write_ = (write_ + 1) % n;
    if (filled_ < n) {
        ++filled_;
    }

    std::size_t best = n;
    double best_score = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < n; ++i) {
        if (!slots_[i].valid) {
            continue;
        }
        const double age_ms =
            std::chrono::duration<double, std::milli>(now - slots_[i].stamped).count();
        const double delta = age_ms - delay_ms;
        const double score = (delta >= 0.0) ? delta : (100000.0 - age_ms);
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best >= n || !slots_[best].tex) {
        return live_srv;
    }
    return slots_[best].tex->srv();
}

} // namespace rxext::genesis_remover
