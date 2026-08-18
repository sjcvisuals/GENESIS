#include "WinPrefix.h"
#include "Device.h"

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

    // ExtSampleD3D: one D3D11 device + shared fence for the whole StreamDevice.
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

SyncDesc Device::before_render() noexcept {
    if (d3d_) {
        d3d_->noteSyncCallback();
        if ((d3d_->syncCallbacks() % 60u) == 1u) {
            removerLog(std::format("Device::before_render fence counter={} completed={} calls={}",
                                   d3d_->fenceCounter(), d3d_->completedValue(),
                                   d3d_->syncCallbacks()));
        }
    }
    return get_sync_desc();
}

void Device::render() noexcept {
    // Do NOT GPU/CPU Wait on the Device fence here.
    // ExtSampleD3D waits then draws entirely on the GPU; our Input path does
    // CopyResource+Map on the CPU. Waiting for ++fenceCounter (signaled only in
    // after_render, or by PIXERA for GPU consumers) deadlocks: Map waits for the
    // GPU Wait, host never reaches after_render Signal → solid black, no STATUS.
    // Keep SyncDesc publish + after_render Signal so PIXERA can wait on OUTPUT.
    if (d3d_) {
        d3d_->noteSyncCallback();
    }
}

SyncDesc Device::after_render() noexcept {
    if (d3d_) {
        d3d_->noteSyncCallback();
        d3d_->signal(++d3d_->fenceCounter());
        if (d3d_->context()) {
            d3d_->context()->Flush();
        }
    }
    return get_sync_desc();
}

SyncDesc Device::get_sync_desc() noexcept {
    if (!d3d_ || !d3d_->fenceShareHandle()) {
        return {};
    }
    return {
        SyncStrategy::TimelineSemaphore,
        ShareHandle{
            HandleType::D3D_FENCE,
            reinterpret_cast<Handle>(d3d_->fenceShareHandle()),
            static_cast<std::uint64_t>(GetCurrentProcessId()),
        },
        d3d_->fenceCounter(),
    };
}

} // namespace rxext::genesis_remover
