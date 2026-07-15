
using uint32_t = unsigned int;
using uint8_t = unsigned char;

// sizes and offsets in uint32_t steps
extern "C"
__global__ void copy_buffer_to_packets_4(
  const uint32_t* __restrict__ buffer,
  uint32_t* __restrict__ packets,
  size_t buffer_pitch,
  size_t first_packet_in_chunk,
  size_t packet_count,
  size_t packets_per_chunk,
  size_t packets_per_line,
  size_t packet_size,
  size_t packet_stride) {

  const size_t local_id = threadIdx.x;
  const size_t local_packet_id = blockIdx.x * blockDim.y + threadIdx.y;
  if (local_packet_id >= packets_per_chunk)
    return;

  const size_t packet_id = local_packet_id + first_packet_in_chunk;
  if (packet_id >= packet_count)
    return;

  const size_t line = packet_id / packets_per_line;
  const size_t packet_in_line = packet_id % packets_per_line;

  const size_t buffer_offset = line * buffer_pitch + packet_in_line * packet_size;
  const size_t packet_offset = local_packet_id * packet_stride;

  for (size_t offset_in_packet = local_id; offset_in_packet < packet_size; offset_in_packet += blockDim.x) {
    const uint32_t value = buffer[buffer_offset + offset_in_packet];
    packets[packet_offset + offset_in_packet] = value;
  }
}

extern "C"
__global__ void copy_buffer_to_packets_1(
  const uint8_t* __restrict__ buffer,
  uint8_t* __restrict__ packets,
  size_t buffer_pitch,
  size_t first_packet_in_chunk,
  size_t packet_count,
  size_t packets_per_chunk,
  size_t packets_per_line,
  size_t packet_size,
  size_t packet_stride) {

  const size_t local_id = threadIdx.x;
  const size_t local_packet_id = blockIdx.x * blockDim.y + threadIdx.y;
  if (local_packet_id >= packets_per_chunk)
    return;

  const size_t packet_id = local_packet_id + first_packet_in_chunk;
  if (packet_id >= packet_count)
    return;

  const size_t line = packet_id / packets_per_line;
  const size_t packet_in_line = packet_id % packets_per_line;

  const size_t buffer_offset = line * buffer_pitch + packet_in_line * packet_size;
  const size_t packet_offset = local_packet_id * packet_stride;

  for (size_t offset_in_packet = local_id; offset_in_packet < packet_size; offset_in_packet += blockDim.x) {
    const uint8_t value = buffer[buffer_offset + offset_in_packet];
    packets[packet_offset + offset_in_packet] = value;
  }
}