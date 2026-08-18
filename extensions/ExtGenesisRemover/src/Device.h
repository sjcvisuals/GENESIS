#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "SharedD3d11.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace rxext::genesis_remover {

class Device : public rxext::StreamDevice {
public:
    explicit Device(ValueSet settings);
    bool initialize() noexcept override;
    string get_property(string_view name) noexcept override;
    vector<ValueSet> enumerate_stream_settings() noexcept override;
    InputStream* create_input_stream(ValueSet settings) noexcept override;

    // ExtSampleD3D: Device-level D3D_FENCE timeline so PIXERA can write Video In.
    SyncDesc before_render() noexcept override;
    void render() noexcept override;
    SyncDesc after_render() noexcept override;

    std::shared_ptr<SharedD3d11> d3d() const { return d3d_; }
    std::uint64_t adapterLuid() const { return adapter_luid_; }

private:
    SyncDesc get_sync_desc() noexcept;

    ValueSet settings_;
    std::uint64_t adapter_luid_ = 0;
    std::shared_ptr<SharedD3d11> d3d_;
};

} // namespace rxext::genesis_remover
