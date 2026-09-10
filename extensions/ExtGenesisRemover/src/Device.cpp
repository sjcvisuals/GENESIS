#include "WinPrefix.h"
#include "Device.h"

#include "AdapterGpu.h"
#include "Input.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "rxext_util.h"

#include <Windows.h>
#include <d3d11.h>

#include <format>
#include <stdexcept>

namespace rxext::genesis_remover {

Device::Device(ValueSet settings) : settings_(std::move(settings)) {
    removerLog("Device ctor (v58 no-rvm-reload; ExtOnnxVision-style: no timeline fence)");
}

bool Device::initialize() noexcept try {
    const string luid_hex = settings_.get(SettingNames::adapter_luid, string("0000000000000000"));
    adapter_luid_ = common::from_hex_string<std::uint64_t>(luid_hex);
    removerLog(std::format("Device::initialize settings adapter_luid={:016x}", adapter_luid_));

    // PIXERA usually injects the engine GPU LUID. When left at 0, auto-pick the first
    // GPU that has Windows displays attached (prefer NVIDIA).
    if (adapter_luid_ == 0) {
        const std::uint64_t picked = pickAdapterLuidWithDisplays();
        if (picked != 0) {
            adapter_luid_ = picked;
            removerLog(std::format("Device::initialize AUTO adapter_luid={:016x} ({})",
                                   adapter_luid_, adapterDescription(adapter_luid_)));
            host().log_info(std::format("GENESIS Remover auto GPU {:016x} ({})", adapter_luid_,
                                        adapterDescription(adapter_luid_)));
        } else {
            removerLog("WARNING: adapter_luid is 0 and auto-pick found no GPU");
            host().log_error(
                "GENESIS Remover: adapter LUID is 0 - set Adapter LUID or connect a display");
        }
    } else {
        removerLog(std::format("Device::initialize using PIXERA/injected LUID {:016x} ({})",
                               adapter_luid_, adapterDescription(adapter_luid_)));
        host().log_info(std::format("GENESIS Remover device ready (adapter_luid={:016x} {})",
                                    adapter_luid_, adapterDescription(adapter_luid_)));
    }

    // Own D3D11 device for shared Video In / output / capture (ExtOnnxVision pattern).
    // Do NOT publish a TimelineSemaphore fence — ExtOnnxVision proves shared D3D11_IMAGE
    // works without it, and our fence Wait turned host Map stalls into sticky low FPS.
    d3d_ = std::make_shared<SharedD3d11>(adapter_luid_);
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
    if (!d3d_) {
        throw std::runtime_error("Device D3D11 not initialized");
    }
    return new Input(d3d_, adapter_luid_, std::move(settings));
} catch (const std::exception& ex) {
    removerLog(std::format("create_input_stream exception: {}", ex.what()));
    host().log_error(ex.what());
    return nullptr;
}

} // namespace rxext::genesis_remover
