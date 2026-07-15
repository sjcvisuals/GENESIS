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

  -- Drive the demo sphere from the timeline (Position purpose).
  stream.position = host.create_node("Keyframe", {
    keys = {
      { time = 0, value = { 0.15, 0.525, -0.35 } },
      { time = 4, value = { 0.15, 1.1, -0.35 } },
    },
    loop = true,
  })

  -- stream.scene_resource = "scenes/demo.gns"
end

dofile('data/scripts/extension_in.lua')
