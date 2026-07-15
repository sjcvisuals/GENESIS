
dofile('data/scripts/standalone.lua')

local opened_displays = 0
local graphics_device_index = 0
displays = open_displays(function(device, output)
  if opened_displays < 1 then
    graphics_device_index = device.index()
    opened_displays = opened_displays + 1
    return true
  end
end)

local opened_streams = 0
links = create_audio_output_links(function(device, channel)
  if device.api() == AUDIO_API and
      string.find(device.name(), AUDIO_DEVICE) and
      opened_streams < 2 then
    opened_streams = opened_streams + 1
    return true
  end
end)

extension = load_extension(EXTENSION_NAME, RUN_IN_SUBPROCESS or false)
device_manager = extension.stream_device_manager()

quads = { }

local device_settings = DEVICE_SETTINGS or { }

-- evaluate in next graph update so adapter_luid is available
set_timeout(function()
  local adapter_luid = g_display_manager.get_adapter_luid(graphics_device_index)
  device_settings.adapter_luid = adapter_luid

  if EXTENSION_DEVICE_INDEX then
    device_manager.configure_device(EXTENSION_DEVICE_INDEX, settings_to_string(device_settings), { }, { })
    device = device_manager.get_device(EXTENSION_DEVICE_INDEX)
  else
    device = device_manager.create_device(settings_to_string(device_settings))
  end

  -- STREAM_SETTINGS may be a single settings table, or an array of settings tables
  STREAM_SETTINGS = STREAM_SETTINGS or { }
  if #STREAM_SETTINGS == 0 then
    STREAM_SETTINGS = { STREAM_SETTINGS }
  end

  local active_stream_settings = { }
  local active_stream_settings_strings = { }
  for index = 1, #STREAM_SETTINGS do
    local stream_settings = STREAM_SETTINGS[index]
    stream_settings.id = index
    stream_settings.is_input = true
    stream_settings.adapter_luid = adapter_luid

    -- find stream when none was specified
    while not stream_settings.handle do
      local streams = device.get_dynamic_streams()
      for i = index, #streams do
        local info = string_to_settings(streams[i])
        if not SELECT_STREAM or SELECT_STREAM(info) then
          stream_settings.handle = info.handle
          break
        end
      end
      sleep(1)
    end

    table.insert(active_stream_settings, stream_settings)
    table.insert(active_stream_settings_strings, settings_to_string(stream_settings))
  end

  device.configure_streams(active_stream_settings_strings)

  local render_queue = host.create_node("RenderQueue")
  for _, stream_settings in ipairs(active_stream_settings) do
    local stream = device.get_stream(stream_settings.id)
    set_upload_devices(stream)

    if SET_STREAM_INPUTS then SET_STREAM_INPUTS(stream, stream_settings) end

    local nx, ny = fields_xy(#active_stream_settings)
    local index = stream_settings.id - 1
    local x = index % nx
    local y = math.floor(index / nx)
    local quad = create_quad_mesh_render_pipe{
      rect = { x, y, 1.0 / nx, 1.0 / ny },
      sampler = stream,
      --topdown = true,
    }
    render_queue.add_render_node(quad)
    table.insert(quads, quad)

    audio = create_audio_object{
      stream = stream.audio_stream
    }
    for index, link in ipairs(links) do
      link.stream = audio.stream
      link.set_stream_channel((index - 1) % 2)
    end
  end

  for _, display in ipairs(displays) do
    display.render = render_queue.render
  end

end, 0.1)
