
#include "Output.h"
#include <d3d11.h>

namespace rxext::sample_d3d {

DXGI_FORMAT get_dxgi_format(Format format);

Output::Output(std::shared_ptr<d3d11::Device> d3d_device, const ValueSet& settings)
  : m_d3d_device(d3d_device),
    m_window_x(settings.get<int>("window_x", 100)),
    m_window_y(settings.get<int>("window_y", 100)),
    m_resolution_x(settings.get<int>(SettingNames::resolution_x, 1920)),
    m_resolution_y(settings.get<int>(SettingNames::resolution_y, 1080)),
    m_format(get_format_by_name(settings.get(SettingNames::format))),
    m_swap_interval(settings.get<bool>(SettingNames::free_run) ? 0 : 1) {
}

bool Output::initialize() noexcept try {
  m_d3d_window = std::make_unique<d3d11::Window>(m_d3d_device->device(),
    m_window_x, m_window_y, m_resolution_x, m_resolution_y);

  m_d3d_quad = std::make_unique<d3d11::Quad>(m_d3d_device->device());

  m_shared_texture = std::make_unique<d3d11::Texture>(m_d3d_device->device(),
    m_resolution_x, m_resolution_y, get_dxgi_format(m_format),
    d3d11::Texture::RenderTarget);

  auto desc = TextureDesc{};
  desc.width = m_resolution_x;
  desc.height = m_resolution_y;
  desc.format = m_format;
  desc.share_handle = {
    HandleType::D3D11_IMAGE,
    m_shared_texture->share_handle(),
  };
  m_target_texture = host().create_texture(desc);

  return true;
}
catch (const std::exception& ex) {
  host().log_error(std::string("initializing output failed: ") + ex.what());
  return false;
}

ValueSet Output::get_state() noexcept {
  auto state = ValueSet();
  state.set(StateNames::resolution_x, m_resolution_x);
  state.set(StateNames::resolution_y, m_resolution_y);
  state.set(StateNames::format, get_format_name(m_format));
  state.set(StateNames::scale_y, -1);
  return state;
}

TextureRef Output::get_target() noexcept {
  m_d3d_window->process_messages();

  return m_target_texture;
}

void Output::present() noexcept try {
  auto d3d_context = m_d3d_device->device_context();

  auto render_target_view = m_d3d_window->render_target_view();
  d3d_context->OMSetRenderTargets(1, &render_target_view, nullptr);

  auto viewport = D3D11_VIEWPORT{ 0, 0, static_cast<FLOAT>(m_resolution_x),
                                        static_cast<FLOAT>(m_resolution_y), 0, 1 };
  d3d_context->RSSetViewports(1, &viewport);

  m_d3d_quad->draw(d3d_context, m_shared_texture->shader_resource_view());
}
catch (const std::exception& ex) {
  host().log_error(std::string("rendering output failed: ") + ex.what());
}

void Output::swap() noexcept {
  m_d3d_window->present(m_swap_interval);
}

} // namespace
