
EXTENSION_NAME = "ExtSampleCPU"

STREAM_SETTINGS = {
  {
    -- select 1 or 2, or nothing to select the first enumerated
    handle = 1,
  },
  {
    handle = 2,
  },
}

dofile('data/scripts/extension_in.lua')
