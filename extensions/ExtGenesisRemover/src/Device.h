#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "SharedD3d11.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace rxext::genesis_remover {

// Matches ExtOnnxVision (blackdown/pixera-onnx-plugin): no Device-level
// TimelineSemaphore fence. Publishing D3D_FENCE + GPU Wait in render() made
// PIXERA presents stall whenever the matte host Mapped the shared capture on
// the same GPU (sticky 2-3fps after enabling outputs).
class Device : public rxext::StreamDevice {
public:
    explicit Device(ValueSet settings);
    bool initialize() noexcept override;
    string get_property(string_view name) noexcept override;
    vector<ValueSet> enumerate_stream_settings() noexcept override;
    InputStream* create_input_stream(ValueSet settings) noexcept override;

    std::shared_ptr<SharedD3d11> d3d() const { return d3d_; }
    std::uint64_t adapterLuid() const { return adapter_luid_; }

private:
    ValueSet settings_;
    std::uint64_t adapter_luid_ = 0;
    std::shared_ptr<SharedD3d11> d3d_;
};

} // namespace rxext::genesis_remover
