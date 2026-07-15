
#include "Device.h"
#include "Input.h"
#include "Output.h"
#include <format>

namespace rxext::sample_d3d {

Device::Device(ValueSet settings)
  : m_settings(std::move(settings)) {
}

bool Device::initialize() noexcept {
  m_d3d_device = std::make_shared<d3d11::Device>(
    m_settings.get(SettingNames::adapter_luid));

  m_d3d_fence = std::make_unique<d3d11::Fence>(m_d3d_device->device_5());

  const auto filename = m_settings.get(SettingNames::filename);
  host().log_info(std::format("loading file '{}'", filename));
  return true;
}

string Device::get_property(string_view name) noexcept {
  if (name == PropertyNames::name)
    return string("SampleD3D");

  if (name == PropertyNames::input_stream_settings_desc) {
    auto settings_desc = SettingsDescBuilder();
    settings_desc.add_int(SettingNames::resolution_x, "Resolution X", 128, 8192);
    settings_desc.add_int(SettingNames::resolution_y, "Resolution Y", 128, 8192);
    settings_desc.add_bool("red_channel_only", "Red channel only");
    settings_desc.add_double("unused_double", "Unused double", 0.0, 1.0);
    settings_desc.add_string("unused_string", "Unused string");
    settings_desc.add_enum("unused_enum", "Unused enum", { "Option 1", "Option 2", "Option 3" });
    return settings_desc.str();
  }
  return { };
}

vector<ValueSet> Device::enumerate_stream_settings() noexcept {
  // return identification of streams found in file
  auto streams = vector<ValueSet>();
  for (auto i = 0; i < 3; ++i) {
    auto& stream = streams.emplace_back();
    stream.set(SettingNames::name, std::format("Stream {}", i));
    stream.set(SettingNames::handle, std::format("handle_{}", i));
    stream.set(SettingNames::resolution_x, 1920);
    stream.set(SettingNames::resolution_y, 1080);
  }
  return streams;
}

InputStream* Device::create_input_stream(ValueSet settings) noexcept try {
  return new Input(m_d3d_device, std::move(settings));
}
catch (const std::exception& ex) {
  host().log_error(ex.what());
  return nullptr;
}

OutputStream* Device::create_output_stream(ValueSet settings) noexcept try {
  return new Output(m_d3d_device, std::move(settings));
}
catch (const std::exception& ex) {
  host().log_error(ex.what());
  return nullptr;
}

SyncDesc Device::before_render() noexcept {
  return get_sync_desc();
}

void Device::render() noexcept {
  // synchronize with end of usage
  m_d3d_fence->wait(m_d3d_device->device_context_4(), ++m_fence_counter);
}

SyncDesc Device::after_render() noexcept {
  // signal end of update
  m_d3d_fence->signal(m_d3d_device->device_context_4(), ++m_fence_counter);

  return get_sync_desc();
}

SyncDesc Device::get_sync_desc() noexcept {
  return {
    SyncStrategy::TimelineSemaphore,
    ShareHandle{
      HandleType::D3D_FENCE,
      m_d3d_fence->share_handle(),
    },
    m_fence_counter,
  };
}

} // namespace
