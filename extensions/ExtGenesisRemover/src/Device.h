#pragma once

#include "rxext_client.h"

#include <cstdint>

namespace rxext::genesis_remover {

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
