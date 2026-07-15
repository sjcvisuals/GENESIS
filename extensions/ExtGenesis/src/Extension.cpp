#include "Extension.h"

#include "Device.h"
#include "rxext.h"
#include "rxext_util.h"

namespace rxext::genesis_ext {

bool Extension::initialize() noexcept {
    host().log_info("GENESIS RenderBridge extension initialized");
    return true;
}

string Extension::get_property(string_view name) noexcept {
    if (name == PropertyNames::name) {
        return string("GENESIS");
    }
    if (name == PropertyNames::api_version) {
        return string(api_version);
    }
    if (name == PropertyNames::stream_device_settings_desc) {
        SettingsDescBuilder settings_desc;
        settings_desc.add_string(SettingNames::adapter_luid, "Adapter LUID (hex)");
        return settings_desc.str();
    }
    return {};
}

StreamDevice* Extension::create_stream_device(ValueSet settings) noexcept try {
    return new Device(std::move(settings));
} catch (const std::exception& ex) {
    host().log_error(ex.what());
    return nullptr;
}

vector<ValueSet> Extension::enumerate_stream_device_settings() noexcept {
    ValueSet device;
    device.set(SettingNames::name, string("GENESIS"));
    device.set(SettingNames::adapter_luid, string("0000000000000000"));
    return {device};
}

} // namespace rxext::genesis_ext

//-------------------------------------------------------------------------

rxext::ExtensionP* rxext_open() {
    return new rxext::genesis_ext::Extension();
}

void rxext_close(rxext::ExtensionP* extension) {
    delete static_cast<rxext::genesis_ext::Extension*>(extension);
}
