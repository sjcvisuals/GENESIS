#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include <cstdint>
#include <utility>

namespace rxext::genesis_remover {

// Host-texture Remover device: no D3D11/Vulkan shared images.
// PIXERA fills a host Video In texture; we download → matte → upload sampler.
class Device : public rxext::StreamDevice {
public:
    explicit Device(ValueSet settings);
    bool initialize() noexcept override;
    string get_property(string_view name) noexcept override;
    vector<ValueSet> enumerate_stream_settings() noexcept override;
    InputStream* create_input_stream(ValueSet settings) noexcept override;

private:
    ValueSet settings_;
    std::uint64_t adapter_luid_ = 0;
};

} // namespace rxext::genesis_remover
