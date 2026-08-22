#pragma once

#include "WinPrefix.h"

#include "D3d11SharedTexture.h"
#include "SharedD3d11.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

struct ID3D11ShaderResourceView;

namespace rxext::genesis_remover {

class D3d11Passthrough;

// Ring of private RTs: delay the live camera independently of matte alpha.
// Delay is wall-clock ms (not frame-count), so it tracks matte lag across FPS.
class LiveDelayRing {
public:
    static constexpr std::size_t kMaxFrames = 24;

    void reset();
    // Push current Video In; returns SRV closest to delay_ms behind "now".
    // delay_ms <= 0 returns live_srv unchanged.
    ID3D11ShaderResourceView* pushAndSample(SharedD3d11& d3d, D3d11Passthrough& blit,
                                            ID3D11ShaderResourceView* live_srv,
                                            std::uint32_t width, std::uint32_t height,
                                            double delay_ms);

private:
    struct Slot {
        std::unique_ptr<D3d11SharedTexture> tex;
        std::chrono::steady_clock::time_point stamped{};
        bool valid = false;
    };

    std::vector<Slot> slots_;
    std::size_t write_ = 0;
    std::size_t filled_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace rxext::genesis_remover
