#include "assets/GridMesh.hpp"

#include "assets/MeshUpload.hpp"
#include "renderer/vulkan/Buffer.hpp"

#include <vector>

namespace genesis {

Mesh createEditorGridMesh(const VulkanContext& context, const float half_extent_m, const int /*divisions*/) {
    // Single ground-plane quad; the grid pattern is drawn in grid.frag.
    // Using a triangle mesh (not a line list) avoids driver bugs where LINE_LIST
    // draws were rasterized as filled ground-plane triangles.
    const float h = half_extent_m;
    const std::vector<MeshVertexPN> vertices = {
        MeshVertexPN{-h, 0.0f, -h, 0.0f, 1.0f, 0.0f},
        MeshVertexPN{h, 0.0f, -h, 0.0f, 1.0f, 0.0f},
        MeshVertexPN{h, 0.0f, h, 0.0f, 1.0f, 0.0f},
        MeshVertexPN{-h, 0.0f, h, 0.0f, 1.0f, 0.0f},
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};

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
    out.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    out.local_aabb_min = glm::vec3(-h, -0.01f, -h);
    out.local_aabb_max = glm::vec3(h, 0.01f, h);
    return out;
}

} // namespace genesis
