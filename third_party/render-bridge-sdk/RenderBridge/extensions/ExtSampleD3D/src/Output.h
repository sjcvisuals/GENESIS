#pragma once

#include "rxext_client.h"
#include "d3d11/d3d11.h"

namespace rxext::sample_d3d {

class Output : public rxext::OutputStream {
public:
  Output(std::shared_ptr<d3d11::Device> d3d_device, const ValueSet& settings);

  bool initialize() noexcept override;
  ValueSet get_state() noexcept override;
  TextureRef get_target() noexcept override;
  void present() noexcept override;
  void swap() noexcept override;

private:
  const int m_window_x;
  const int m_window_y;
  const int m_resolution_x;
  const int m_resolution_y;
  const int m_swap_interval;
  const Format m_format;
  std::shared_ptr<d3d11::Device> m_d3d_device;
  std::unique_ptr<d3d11::Window> m_d3d_window;
  std::unique_ptr<d3d11::Texture> m_shared_texture;
  std::unique_ptr<d3d11::Quad> m_d3d_quad;
  TextureRef m_target_texture;
};

} // namespace
