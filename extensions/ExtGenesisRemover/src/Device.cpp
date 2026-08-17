#include "WinPrefix.h"
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
    const string luid_hex = settings_.get(SettingNames::adapter_luid, string("0000000000000000"));
    adapter_luid_ = common::from_hex_string<std::uint64_t>(luid_hex);
    removerLog(std::format("Device::initialize adapter_luid={:016x}", adapter_luid_));
    host().log_info(std::format("GENESIS Remover device ready (adapter_luid={:016x})",
                                adapter_luid_));
    if (adapter_luid_ == 0) {
        removerLog("WARNING: adapter_luid is 0 - PIXERA must inject the engine GPU LUID "
                   "or shared Video In stays black/orange");
        host().log_error("GENESIS Remover: adapter LUID is 0 - set device Adapter LUID to the "
                         "PIXERA engine GPU or shared textures will stay black");
    }
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
    return new Input(adapter_luid_, std::move(settings));
} catch (const std::exception& ex) {
    removerLog(std::format("create_input_stream exception: {}", ex.what()));
    host().log_error(ex.what());
    return nullptr;
}

} // namespace rxext::genesis_remover
