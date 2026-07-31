#pragma once

#include "WinPrefix.h"

// Parse RenderBridge headers before D3D/Windows (ExtSampleD3D order).
#include "rxext_client.h"

#include "d3d11/d3d11.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace rxext::genesis_remover {

class Device : public rxext::StreamDevice {
public:
    explicit Device(ValueSet settings);
    bool initialize() noexcept override;
    string get_property(string_view name) noexcept override;
    vector<ValueSet> enumerate_stream_settings() noexcept override;
    InputStream* create_input_stream(ValueSet settings) noexcept override;

    SyncDesc before_render() noexcept override;
    void render() noexcept override;
    SyncDesc after_render() noexcept override;

private:
    SyncDesc get_sync_desc() noexcept;

    ValueSet settings_;
    std::string adapter_luid_hex_ = "0000000000000000";
    std::shared_ptr<d3d11::Device> d3d_device_;
    std::unique_ptr<d3d11::Fence> d3d_fence_;
    std::uint64_t fence_counter_ = 0;
};

} // namespace rxext::genesis_remover
