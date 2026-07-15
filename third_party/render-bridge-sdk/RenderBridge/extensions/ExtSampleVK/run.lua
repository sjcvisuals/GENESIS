
EXTENSION_NAME = "ExtSampleVK"

DEVICE_SETTINGS = {
  filename = [[C:\Path\to\file]],
}

STREAM_SETTINGS = {
  {
    handle = "handle_0",
    resolution_x = 1920,
    resolution_y = 1080,
  },
  {
    handle = "handle_1",
    resolution_x = 64,
    resolution_y = 64,
  },
}

SET_STREAM_INPUTS = function(stream)
  -- time
  clock = host.create_node("SystemClock")
  stream.time = clock
end

dofile('data/scripts/extension_in.lua')
