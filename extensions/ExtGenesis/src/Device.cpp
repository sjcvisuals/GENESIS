#include "Device.h"

#include "Input.h"
#include "common/string.h"
#include "rxext_util.h"

namespace rxext::genesis_ext {

Device::Device(ValueSet settings) : settings_(std::move(settings)) {}

bool Device::initialize() noexcept try {
    const string luid_hex = settings_.get(SettingNames::adapter_luid, string("0000000000000000"));
    adapter_luid_ = common::from_hex_string<std::uint64_t>(luid_hex);
    host().log_info(std::format("GENESIS device ready (adapter_luid={:016x})", adapter_luid_));
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS device init failed: {}", ex.what()));
    return false;
}

string Device::get_property(string_view name) noexcept {
    if (name == PropertyNames::name) {
        return string("GENESIS");
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
    ValueSet stream;
    stream.set(SettingNames::name, string("Main"));
    stream.set(SettingNames::handle, string("main"));
    stream.set(SettingNames::resolution_x, static_cast<std::uint32_t>(1920));
    stream.set(SettingNames::resolution_y, static_cast<std::uint32_t>(1080));
    return {stream};
}

InputStream* Device::create_input_stream(ValueSet settings) noexcept try {
    return new Input(adapter_luid_, std::move(settings));
} catch (const std::exception& ex) {
    host().log_error(ex.what());
    return nullptr;
}

} // namespace rxext::genesis_ext
