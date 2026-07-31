#include "Device.h"

#include "Input.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "rxext_util.h"

#include <format>

namespace rxext::genesis_remover {

Device::Device(ValueSet settings) : settings_(std::move(settings)) {
    removerLog("Device ctor");
}

bool Device::initialize() noexcept try {
    adapter_luid_hex_ = std::string(settings_.get(SettingNames::adapter_luid, string("0000000000000000")));
    removerLog(std::format("Device::initialize adapter_luid={}", adapter_luid_hex_));

    // ExtSampleD3D pattern: create the D3D11 device at device init (before streams).
    d3d_device_ = std::make_shared<d3d11::Device>(adapter_luid_hex_);
    d3d_fence_ = std::make_unique<d3d11::Fence>(d3d_device_->device_5());

    host().log_info(std::format("GENESIS Remover D3D11 device ready (adapter_luid={})", adapter_luid_hex_));
    removerLog("Device::initialize D3D11 OK");
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("Device::initialize exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover device init failed: {}", ex.what()));
    return false;
}

string Device::get_property(string_view name) noexcept {
    if (name == PropertyNames::name) {
        return string("GENESIS Remover");
    }
    if (name == PropertyNames::input_stream_settings_desc) {
        SettingsDescBuilder settings_desc;
        settings_desc.add_int(SettingNames::resolution_x, "Resolution X", 128, 8192);
        settings_desc.add_int(SettingNames::resolution_y, "Resolution Y", 128, 8192);
        return settings_desc.str();
    }
    return {};
}

vector<ValueSet> Device::enumerate_stream_settings() noexcept {
    removerLog("Device::enumerate_stream_settings");
    ValueSet stream;
    stream.set(SettingNames::name, string("Main"));
    stream.set(SettingNames::handle, string("main"));
    stream.set(SettingNames::resolution_x, static_cast<std::uint32_t>(1920));
    stream.set(SettingNames::resolution_y, static_cast<std::uint32_t>(1080));
    return {stream};
}

InputStream* Device::create_input_stream(ValueSet settings) noexcept try {
    removerLog("Device::create_input_stream");
    if (!d3d_device_) {
        throw std::runtime_error("D3D11 device missing");
    }
    return new Input(d3d_device_, std::move(settings));
} catch (const std::exception& ex) {
    removerLog(std::format("create_input_stream exception: {}", ex.what()));
    host().log_error(ex.what());
    return nullptr;
}

SyncDesc Device::before_render() noexcept {
    return get_sync_desc();
}

void Device::render() noexcept {
    // ExtSampleD3D: wait for host to finish using shared textures.
    if (d3d_fence_ && d3d_device_) {
        d3d_fence_->wait(d3d_device_->device_context_4(), ++fence_counter_);
    }
}

SyncDesc Device::after_render() noexcept {
    if (d3d_fence_ && d3d_device_) {
        d3d_fence_->signal(d3d_device_->device_context_4(), ++fence_counter_);
    }
    return get_sync_desc();
}

SyncDesc Device::get_sync_desc() noexcept {
    if (!d3d_fence_) {
        return {};
    }
    return {
        SyncStrategy::TimelineSemaphore,
        ShareHandle{
            HandleType::D3D_FENCE,
            d3d_fence_->share_handle(),
        },
        fence_counter_,
    };
}

} // namespace rxext::genesis_remover
