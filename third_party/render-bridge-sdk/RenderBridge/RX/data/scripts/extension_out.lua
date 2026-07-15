
dofile('data/scripts/standalone.lua')

extension = load_extension(EXTENSION_NAME, RUN_IN_SUBPROCESS or false)
device_manager = extension.stream_device_manager()

displays = { }

extension_device_index = EXTENSION_DEVICE_INDEX or 0
graphics_device_index = GRAPHICS_DEVICE_INDEX or 0
device_settings = DEVICE_SETTINGS or { }

initialize_render_device(graphics_device_index)

-- evaluate in next graph update so adapter_luid is available
set_timeout(function()
  local adapter_luid = g_display_manager.get_adapter_luid(graphics_device_index)
  device_settings.adapter_luid = adapter_luid

  if device_manager.device_count() > 0 then
    device_manager.configure_device(extension_device_index, settings_to_string(device_settings), { }, { })
    device = device_manager.get_device(extension_device_index)
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
    stream_settings.adapter_luid = adapter_luid
    table.insert(active_stream_settings, stream_settings)
    table.insert(active_stream_settings_strings, settings_to_string(stream_settings))
  end

  local render_device = g_display_manager.get_render_device(graphics_device_index)
  for _, stream_settings in ipairs(active_stream_settings) do
    device.set_stream_render_device(stream_settings.id, render_device)
  end

  device.configure_streams(active_stream_settings_strings)

  for _, stream_settings in ipairs(active_stream_settings) do
    local stream = device.get_stream(stream_settings.id)

    local quad = create_quad_render_pipe{
      sampler = create_video_sampler{
        settings = {
          filename = VIDEO_FILE,
          frame_rate = FRAME_RATE,
          loop = true
        }
      }
    }

    display = open_custom_display{
      device_index = graphics_device_index,
      stream = stream,
    }
    display.render = quad.render
    table.insert(displays, display)

    if AUDIO_FILE then
      audio = create_audio_object{
        settings = {
          filename = AUDIO_FILE,
          loop = true
        }
      }
      stream.audio_stream = audio.stream
    end
  end
end, 1)