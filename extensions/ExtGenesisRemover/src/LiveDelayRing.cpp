#include "WinPrefix.h"
#include "LiveDelayRing.h"

#include "D3d11Passthrough.h"
#include "rxext.h"

#include <limits>

namespace rxext::genesis_remover {

void LiveDelayRing::reset() {
    slots_.clear();
    write_ = 0;
    filled_ = 0;
    width_ = 0;
    height_ = 0;
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

    if (slots_.size() != kMaxFrames || width_ != width || height_ != height) {
        slots_.clear();
        slots_.resize(kMaxFrames);
        for (std::size_t i = 0; i < kMaxFrames; ++i) {
            slots_[i].tex = std::make_unique<D3d11SharedTexture>(d3d.device(), d3d.context());
            slots_[i].tex->allocate(width, height, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                    /*publish_share=*/false);
            slots_[i].valid = false;
        }
        write_ = 0;
        filled_ = 0;
        width_ = width;
        height_ = height;
    }

    const auto now = std::chrono::steady_clock::now();
    // Store Video In in texture orientation (display path still flips).
    blit.draw(d3d.context(), live_srv, slots_[write_].tex->rtv(), width, height, /*flip_v=*/false);
    slots_[write_].stamped = now;
    slots_[write_].valid = true;
    write_ = (write_ + 1) % kMaxFrames;
    if (filled_ < kMaxFrames) {
        ++filled_;
    }

    // Prefer a frame at least as old as delay_ms; among those, closest to the target age.
    // If the ring is still filling, take the oldest available.
    std::size_t best = kMaxFrames;
    double best_score = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < kMaxFrames; ++i) {
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
    if (best >= kMaxFrames || !slots_[best].tex) {
        return live_srv;
    }
    return slots_[best].tex->srv();
}

} // namespace rxext::genesis_remover
