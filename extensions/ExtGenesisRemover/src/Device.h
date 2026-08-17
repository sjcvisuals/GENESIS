#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include <cstdint>
#include <utility>

namespace rxext::genesis_remover {

// Shared-Vulkan Remover device (same I/O model as ExtGenesis Background Remover).
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
