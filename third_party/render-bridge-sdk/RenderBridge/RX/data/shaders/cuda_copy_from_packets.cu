
using uint32_t = unsigned int;
using uint16_t = unsigned short;
using uint8_t = unsigned char;

extern "C"
__global__ void copy_packets_to_buffer_1(
  uint8_t* __restrict__ dst,
  size_t dst_size,
  const uint32_t* __restrict__ schedule,
  size_t schedule_entries,
  const uint8_t* __restrict__ chunk_data,
  size_t stride_size,
  size_t first_packet,
  size_t first_span,
  size_t span_count) {

  const size_t relative_span = blockIdx.x * blockDim.y + threadIdx.y;
  const size_t threads_per_packet = blockDim.x;

  if (relative_span >= span_count)
    return;

  size_t span = relative_span + first_span;

  // fetch schedule entries
  const uint32_t destination_offset = schedule[span];
  const uint32_t packet_id = schedule[span + schedule_entries];
  const uint32_t data_size_and_stride_offset = schedule[span + 2 * schedule_entries];

  const uint32_t data_size = data_size_and_stride_offset >> 16;
  const uint32_t stride_offset = data_size_and_stride_offset & 0xFFFF;

  const size_t chunk_offset = stride_offset + stride_size * (packet_id - first_packet);
  for (size_t i = threadIdx.x; i < data_size; i += threads_per_packet) {
    dst[destination_offset + i] = chunk_data[chunk_offset + i];
  }
}

