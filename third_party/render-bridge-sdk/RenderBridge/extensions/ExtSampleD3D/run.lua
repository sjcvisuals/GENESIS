
EXTENSION_NAME = "ExtSampleD3D"
--RUN_IN_SUBPROCESS = true

if true then
  -- input
  DEVICE_SETTINGS = {
    filename = [[C:\Path\to\file]],
    frame_rate = 50,
  }

  STREAM_SETTINGS = {
    {
      -- when not set, nth handle returned by Device::enumerate_stream_settings is used
      -- handle = "handle_0",

      resolution_x = 1920,
      resolution_y = 1080,
    },
    {
      resolution_x = 1920,
      resolution_y = 1080,
    }
  }

  SET_STREAM_INPUTS = function(stream, stream_settings)
    -- time
    local clock = host.create_node("SystemClock")
    stream.time = clock

    -- color
    local colors = {
      Vector4(1.0, 0.5, 0.25, 1.0),
      Vector4(0.25, 0.5, 1.0, 1.0),
    }
    local color = host.create_node("Vector4")
    color.set_value(colors[stream_settings.id])
    stream.color = color.value

    -- texture
    local filenames = {
      'data/test/sucher_HD_h264.mp4',
      'data/test/sucher_HD_h264.mp4'
    }
    local video = create_video_sampler{
      settings = {
        filename = filenames[stream_settings.id],
        loop = true,
      }
    }
    stream.texture = resolve_sampler(video, 1280, 720)

    -- text
    stream.set_text(Data("String", "Sample text"))

    -- data
    stream.set_data(Data("Uint8", { 1, 2, 3, 4, 5 }))
  end

  dofile('data/scripts/extension_in.lua')
else
  -- output
  GRAPHICS_DEVICE_INDEX = 0
  VIDEO_FILE = 'data/test/sucher_HD_h264.mp4'
  FRAME_RATE = 60
  --AUDIO_FILE = 'data/test/music.mp3'

  STREAM_SETTINGS = {
    {
      handle = 'Output 1',
      resolution_x = 1920 / 3,
      resolution_y = 1080 / 3,
      window_x = 100,
      window_y = 100,
      frame_rate = FRAME_RATE,
      sync_group = 0,
    },
    {
      handle = 'Output 2',
      resolution_x = 1920 / 3,
      resolution_y = 1080 / 3,
      window_x = 100 + 1920 / 3,
      window_y = 100,
      frame_rate = FRAME_RATE,
      sync_group = 0,
    },
  }
  dofile('data/scripts/extension_out.lua')
end
