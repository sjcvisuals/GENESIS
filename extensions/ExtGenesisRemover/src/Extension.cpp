#include "WinPrefix.h"
#include "Extension.h"

#include "Device.h"
#include "RemoverLog.h"
#include "rxext.h"
#include "rxext_util.h"

#include <format>

namespace rxext::genesis_remover {

bool Extension::initialize() noexcept {
    removerLog("Extension::initialize");
    host().log_info(std::format("GENESIS Remover extension initialized ({})", kRemoverBuildStamp));
    return true;
}

string Extension::get_property(string_view name) noexcept {
    // Keep property surface aligned with ExtGenesis (known-good in PIXERA).
    if (name == PropertyNames::name) {
        return string("GENESIS Remover");
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
    removerLog("Extension::create_stream_device");
    return new Device(std::move(settings));
} catch (const std::exception& ex) {
    removerLog(std::format("create_stream_device exception: {}", ex.what()));
    host().log_error(ex.what());
    return nullptr;
}

vector<ValueSet> Extension::enumerate_stream_device_settings() noexcept {
    removerLog("Extension::enumerate_stream_device_settings");
    ValueSet device;
    device.set(SettingNames::name, string("GENESIS Remover"));
    device.set(SettingNames::adapter_luid, string("0000000000000000"));
    return {device};
}

} // namespace rxext::genesis_remover

RXEXT_API rxext::ExtensionP* rxext_open() {
    rxext::genesis_remover::removerLog("rxext_open");
    return new rxext::genesis_remover::Extension();
}

RXEXT_API void rxext_close(rxext::ExtensionP* extension) {
    rxext::genesis_remover::removerLog("rxext_close");
    delete static_cast<rxext::genesis_remover::Extension*>(extension);
}
