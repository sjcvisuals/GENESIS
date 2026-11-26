EXTENSION_NAME = "ExtGenesis"

DEVICE_SETTINGS = {
  adapter_luid = "0000000000000000",
}

STREAM_SETTINGS = {
  {
    handle = "genesis_main",
    resolution_x = 1920,
    resolution_y = 1080,
  },
}

SET_STREAM_INPUTS = function(stream)
  clock = host.create_node("SystemClock")
  stream.time = clock

  -- Notch-like clip: pick a packaged scene via show_select (Scene dropdown).
  -- Per-actor Position / expose-from-Genesis is deferred.
  -- stream.show_select = 0
end

dofile('data/scripts/extension_in.lua')
