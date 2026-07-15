#pragma once

#include "rxext_client.h"
#include "d3d11/d3d11.h"

namespace rxext::sample_d3d {

class Device : public rxext::StreamDevice {
public:
  explicit Device(ValueSet settings);
  bool initialize() noexcept override;
  string get_property(string_view name) noexcept override;
  vector<ValueSet> enumerate_stream_settings() noexcept override;
  InputStream* create_input_stream(ValueSet settings) noexcept override;
  OutputStream* create_output_stream(ValueSet settings) noexcept override;
  SyncDesc before_render() noexcept override;
  void render() noexcept override;
  SyncDesc after_render() noexcept override;

private:
  SyncDesc get_sync_desc() noexcept;

  ValueSet m_settings;
  std::shared_ptr<d3d11::Device> m_d3d_device;
  std::unique_ptr<d3d11::Fence> m_d3d_fence;
  uint64_t m_fence_counter{ };
};

} // namespace
