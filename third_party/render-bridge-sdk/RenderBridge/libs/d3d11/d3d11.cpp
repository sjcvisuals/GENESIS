
#include "d3d11/d3d11.h"
#include <stdexcept>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <DXGI.h>
#include <vector>
#include <array>
#include "common/string.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

const SECURITY_ATTRIBUTES& getSecurityAttributes();

namespace d3d11 {

namespace {
  const auto quad_vs = R"(
struct VSInput {
  float4 position : POSITION;
  float2 uv : TEXCOORD0;
};

struct VSOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = input.position;
  output.uv = input.uv;
  return output;
}
)";

  const auto quad_ps = R"(
struct VSOutput {
  float4 Position: SV_Position;
  float2 Uv: TEXCOORD0;
};

sampler sample_linear : register(s0);
Texture2D u_texture : register(t0);

float4 PSMain(VSOutput input) : SV_Target {
  return u_texture.Sample(sample_linear, input.Uv);
}
)";

  ComPointer<IDXGIAdapter1> get_device_adapter(IDXGIFactory1& factory, LUID adapter_luid) {
    auto adapter = ComPointer<IDXGIAdapter1>{ };
    for (auto i = 0u; factory.EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      if (adapter_luid.HighPart == 0 &&
          adapter_luid.LowPart == 0)
        return adapter;

      auto desc = DXGI_ADAPTER_DESC1{};
      adapter->GetDesc1(&desc);
      if (desc.AdapterLuid.HighPart == adapter_luid.HighPart &&
          desc.AdapterLuid.LowPart == adapter_luid.LowPart)
        return adapter;
    }
    return { };
  }

  ComPointer<ID3D11Device> create_device(ComPointer<IDXGIAdapter1> adapter) {
    auto device_flags = 0u;
    const auto feature_levels_requested = D3D_FEATURE_LEVEL_11_0;
    auto device = ComPointer<ID3D11Device>();
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, device_flags,
          &feature_levels_requested, 1, D3D11_SDK_VERSION, &device, nullptr, nullptr)))
      return { };
    return device;
  }

  ComPointer<ID3DBlob> compile_shader(const char* code, const char* entry_point, const char* target) {
    auto result = ComPointer<ID3DBlob>();
    auto error = ComPointer<ID3DBlob>();
		if (FAILED(D3DCompile(code, std::strlen(code), nullptr, nullptr, nullptr,
               entry_point, target, 0, 0, &result, &error)))
      throw std::runtime_error("compiling shader failed: " +
        std::string(error ? static_cast<const char*>(error->GetBufferPointer()) : ""));
    return result;
  }
} // namespace

Device::Device(std::string_view adapter_luid) {
  auto dxgi_factory = ComPointer<IDXGIFactory1>{ };
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory))))
    m_device = create_device(get_device_adapter(*dxgi_factory,
      common::from_hex_string<LUID>(adapter_luid)));
  if (!m_device)
    throw std::runtime_error("creating D3D11 device failed");

  m_device_5 = com_cast<ID3D11Device5>(m_device);
  if (!m_device_5)
    throw std::runtime_error("obtaining D3D11_5 device failed");

  m_device->GetImmediateContext(&m_device_context);
  m_device_context_4 = com_cast<ID3D11DeviceContext4>(m_device_context);
  if (!m_device_context_4)
    throw std::runtime_error("obtaining D3D11_4 device context failed");
}

Device::~Device() = default;

void Device::clear_target(ID3D11RenderTargetView* render_target_view) {
  float color[4] = { };
  device_context()->ClearRenderTargetView(render_target_view, color);
}

void Device::copy_texture(ID3D11Texture2D* dest, ID3D11Texture2D* source) {
  if (dest != source)
    device_context()->CopyResource(dest, source);
}

Texture::Texture(ID3D11Device* device, size_t width, size_t height,
    DXGI_FORMAT format, Options options)
  : m_width(width), m_height(height), m_format(format) {

  auto desc = D3D11_TEXTURE2D_DESC{ };
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.Format = format;
  desc.ArraySize = 1;
  desc.SampleDesc = { 1, 0 };
  desc.MipLevels = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.CPUAccessFlags = 0;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  if (options & RenderTarget)
    desc.BindFlags |= D3D11_BIND_RENDER_TARGET;

  // SHARED_NTHANDLE | SHARED_KEYEDMUTEX + keyed mutex synchronization:
  //  - AcquireKeyedMutexWin32EXT fails with "memory has no keyed mutex"
  // SHARED_NTHANDLE | SHARED_KEYEDMUTEX + D3D11 Fence:
  //  - no errors but textures remain black
  // SHARED_NTHANDLE (+ D3D11 Fence):
  //  - CreateTexture2D fails
  // what finally worked:
  if (!(options & NotShared))
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

  if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_texture)))
    throw std::runtime_error("creating D3D11 texture failed");

  if (!(options & NotShared)) {
    if (auto dxgi_resource = com_cast<IDXGIResource1>(m_texture))
      dxgi_resource->CreateSharedHandle(&getSecurityAttributes(),
        GENERIC_ALL, nullptr, &m_share_handle);
    if (!m_share_handle)
      throw std::runtime_error("creating D3D11 texture share handle failed");
  }

  auto srv_desc = D3D11_SHADER_RESOURCE_VIEW_DESC{ };
  srv_desc.Format = desc.Format;
  srv_desc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = 1;
  srv_desc.Texture2D.MostDetailedMip = 0;
  if (FAILED(device->CreateShaderResourceView(m_texture, &srv_desc, &m_shader_resource_view)))
    throw std::runtime_error("creating D3D11 shader resource view failed");

  if (options & RenderTarget) {
    if (FAILED(device->CreateRenderTargetView(
        m_texture, NULL, &m_render_target_view)))
      throw std::runtime_error("creating D3D11 render target view failed");
  }
}

Texture::~Texture() = default;

Fence::Fence(ID3D11Device5* device) {
  if (FAILED(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_fence))))
    throw std::runtime_error("creating D3D11 fence failed");

  if (FAILED(m_fence->CreateSharedHandle(&getSecurityAttributes(),
        GENERIC_ALL, nullptr, &m_share_handle)))
    throw std::runtime_error("creating D3D11 fence share handle failed");
}

Fence::~Fence() = default;

void Fence::signal(ID3D11DeviceContext4* device_context, uint64_t value) {
  device_context->Signal(m_fence, value);
}

void Fence::wait(ID3D11DeviceContext4* device_context, uint64_t value) {
  device_context->Wait(m_fence, value);
}

Quad::Quad(ID3D11Device* device, float size) {
  // shaders
  const auto input_element_descs = std::array<D3D11_INPUT_ELEMENT_DESC, 2>{
    D3D11_INPUT_ELEMENT_DESC{
      "POSITION", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position),
      D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0
    },
    D3D11_INPUT_ELEMENT_DESC{
      "TEXCOORD", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv),
      D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0
    }
  };
  auto vs = compile_shader(quad_vs, "VSMain", "vs_5_0");
  auto ps = compile_shader(quad_ps, "PSMain", "ps_5_0");
  if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vertex_shader)) ||
      FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_pixel_shader)) ||
      FAILED(device->CreateInputLayout(input_element_descs.data(), static_cast<UINT>(input_element_descs.size()),
                                       vs->GetBufferPointer(), vs->GetBufferSize(), &m_input_layout)))
    throw std::runtime_error("creating D3D11 shaders failed");

  // sampler
  auto sampler_desc = D3D11_SAMPLER_DESC{ };
  sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
  if (FAILED(device->CreateSamplerState(&sampler_desc, &m_sample_linear)))
    throw std::runtime_error("creating D3D11 sampler failed");

  // vertex buffer
  const auto vertices = std::array<Vertex, 4>{
    Vertex{ -size, -size, 0, 1, 0, 0 },
    Vertex{ -size,  size, 0, 1, 0, 1 },
    Vertex{  size, -size, 0, 1, 1, 0 },
    Vertex{  size,  size, 0, 1, 1, 1 },
  };
  auto buffer_desc = D3D11_BUFFER_DESC{ };
  buffer_desc.ByteWidth = UINT{ vertices.size() * sizeof(Vertex) };
  buffer_desc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
  buffer_desc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
  auto resource_data = D3D11_SUBRESOURCE_DATA{ };
  resource_data.pSysMem = vertices.data();
  if (FAILED(device->CreateBuffer(&buffer_desc, &resource_data, &m_buffer)))
    throw std::runtime_error("creating D3D11 buffer failed");
}

Quad::~Quad() = default;

void Quad::draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture) {
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  const auto stride = UINT{ sizeof(Vertex) };
  const auto offset = UINT{ };
  context->IASetVertexBuffers(0, 1, m_buffer.address_of_pointer(), &stride, &offset);

  context->IASetInputLayout(m_input_layout);
  context->VSSetShader(m_vertex_shader, nullptr, 0);
  context->PSSetShader(m_pixel_shader, nullptr, 0);

  context->PSSetShaderResources(0, 1, &texture);
  context->PSSetSamplers(0, 1, m_sample_linear.address_of_pointer());

  context->Draw(4, 0);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch(message) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hWnd, message, wParam, lParam);
}

Window::Window(ID3D11Device* device, int x, int y, int width, int height) {
  m_update_thread.dispatch([&]() {
    create(device, x, y, width, height);
  });
}

Window::~Window() = default;

void Window::create(ID3D11Device* device, int x, int y, int width, int height) {
  auto instance_handle = GetModuleHandleW(NULL);
  const auto window_class_name = L"ExtSampleD3D";
  const auto window_title = window_class_name;

  auto wc = WNDCLASSEXW{ sizeof(WNDCLASSEXW) };
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &WindowProc;
  wc.hInstance = instance_handle;
  wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
  wc.lpszClassName = window_class_name;
  RegisterClassExW(&wc);

  auto wr = RECT { 0, 0, width, height };
  AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

  auto hwnd = CreateWindowExW(NULL,
    window_class_name, window_title, WS_OVERLAPPEDWINDOW,
    x, y, wr.right - wr.left, wr.bottom - wr.top,
    NULL, NULL, instance_handle, NULL);

  ShowWindow(hwnd, SW_SHOW);

  auto swap_chain_desc = DXGI_SWAP_CHAIN_DESC1{ };
  swap_chain_desc.Width = width;
  swap_chain_desc.Height = height;
  swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = 2;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.Scaling = DXGI_SCALING_NONE;

  auto dxgi_factory = ComPointer<IDXGIFactory2>{ };
  if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&dxgi_factory)) ||
      FAILED(dxgi_factory->CreateSwapChainForHwnd(device, hwnd,
        &swap_chain_desc, NULL, NULL, &m_swap_chain))))
    throw std::runtime_error("creating swap chain failed");

  dxgi_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  auto backbuffer = ComPointer<ID3D11Texture2D>();
  m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
  device->CreateRenderTargetView(backbuffer, NULL, &m_render_target_view);

  m_window_handle = hwnd;
}

bool Window::process_messages() {
  m_update_thread.dispatch([&]() {
    auto msg = MSG{ };
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);

      if (msg.message == WM_QUIT)
        m_closed = true;
    }
  });
  return m_closed;
}

void Window::present(int swap_interval) {
  m_update_thread.dispatch([&]() {
    m_swap_chain->Present(swap_interval, 0);
  });
}

} // namespace