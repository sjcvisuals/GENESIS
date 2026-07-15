#include "assets/MeshUpload.hpp"

#include "renderer/vulkan/Buffer.hpp"

namespace genesis {

Mesh uploadMesh(const VulkanContext& context, std::span<const MeshVertexPN> vertices,
                std::span<const std::uint32_t> indices) {
    Mesh out{};
    const VkDeviceSize vb_size = static_cast<VkDeviceSize>(vertices.size_bytes());
    const VkDeviceSize ib_size = static_cast<VkDeviceSize>(indices.size_bytes());

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

    out.index_count = static_cast<uint32_t>(indices.size());
    return out;
}

} // namespace genesis
