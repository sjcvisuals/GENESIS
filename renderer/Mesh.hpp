#pragma once

#include "renderer/vulkan/Buffer.hpp"

#include <cstdint>
#include <memory>

#include <volk.h>

namespace genesis {

struct Mesh {
    std::unique_ptr<Buffer> vertex_buffer;
    std::unique_ptr<Buffer> index_buffer;
    uint32_t index_count = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

} // namespace genesis
