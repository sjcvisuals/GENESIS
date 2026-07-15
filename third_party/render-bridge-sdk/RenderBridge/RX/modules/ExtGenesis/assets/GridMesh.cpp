#include "assets/GridMesh.hpp"

#include "assets/MeshUpload.hpp"
#include "renderer/vulkan/Buffer.hpp"

#include <cmath>
#include <vector>

namespace genesis {

Mesh createEditorGridMesh(const VulkanContext& context, const float half_extent_m, const int divisions) {
    const int divs = std::max(divisions, 1);
    const float step = (half_extent_m * 2.0f) / static_cast<float>(divs);

    std::vector<MeshVertexPN> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>((divs + 1) * 2 * 2));
    indices.reserve(static_cast<std::size_t>((divs + 1) * 4));

    auto addLine = [&](const MeshVertexPN& a, const MeshVertexPN& b) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(a);
        vertices.push_back(b);
        indices.push_back(base);
        indices.push_back(base + 1);
    };

    for (int i = 0; i <= divs; ++i) {
        const float t = -half_extent_m + static_cast<float>(i) * step;
        const bool major = (i % (divs / 2 == 0 ? divs : divs / 2) == 0) || i == 0 || i == divs;
        (void)major;
        addLine(MeshVertexPN{t, 0.0f, -half_extent_m, 0.0f, 1.0f, 0.0f},
                MeshVertexPN{t, 0.0f, half_extent_m, 0.0f, 1.0f, 0.0f});
        addLine(MeshVertexPN{-half_extent_m, 0.0f, t, 0.0f, 1.0f, 0.0f},
                MeshVertexPN{half_extent_m, 0.0f, t, 0.0f, 1.0f, 0.0f});
    }

    Mesh out{};
    const VkDeviceSize vb_size = static_cast<VkDeviceSize>(vertices.size() * sizeof(MeshVertexPN));
    const VkDeviceSize ib_size = static_cast<VkDeviceSize>(indices.size() * sizeof(std::uint32_t));

    Buffer vb_staging(context, vb_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vb_staging.upload(vertices.data(), vb_size);

    Buffer ib_staging(context, ib_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ib_staging.upload(indices.data(), ib_size);

    out.vertex_buffer = std::make_unique<Buffer>(
        context, vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    out.index_buffer = std::make_unique<Buffer>(
        context, ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    context.immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.size = vb_size;
        vkCmdCopyBuffer(cmd, vb_staging.handle(), out.vertex_buffer->handle(), 1, &copy);
        copy.size = ib_size;
        vkCmdCopyBuffer(cmd, ib_staging.handle(), out.index_buffer->handle(), 1, &copy);
    });

    out.index_count = static_cast<std::uint32_t>(indices.size());
    out.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    return out;
}

} // namespace genesis
