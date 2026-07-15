#version 450

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(binding = 0, rgba8ui) uniform readonly uimage2D u_source;
layout(binding = 0, std430) writeonly buffer ssbo {
  uint data[];
};

shared uint tmp[32][32];

void main() {
  const uint pitch = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
  const uvec2 global_id = gl_GlobalInvocationID.xy;
  const uvec2 local_id = gl_LocalInvocationID.xy;

  const uvec4 color = imageLoad(u_source, ivec2(global_id));

  tmp[local_id.x][local_id.y] =
    (color.r << 16) |
    (color.g <<  8) |
    (color.b <<  0) |
    (color.a << 24);

  memoryBarrierShared();
  barrier();

  const uint addr = global_id.y * pitch + global_id.x;
  if (addr >= data.length())
    return;

  data[addr] = tmp[local_id.x][local_id.y];
}