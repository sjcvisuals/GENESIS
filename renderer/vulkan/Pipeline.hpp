#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include <filesystem>
#include <vector>

namespace genesis {

class VulkanContext;

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path);

struct VertexAttributeDesc {
    uint32_t location = 0;
    VkFormat format = VK_FORMAT_R32G32B32_SFLOAT;
    uint32_t offset = 0;
};

struct GraphicsPipelineConfig {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::filesystem::path vert_shader;
    std::filesystem::path frag_shader;
    VkExtent2D extent{};
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depth_test = false;
    bool depth_write = true;
    bool alpha_blend = false;
    // When alpha_blend: false = SrcAlpha/1-SrcAlpha, true = SrcAlpha/One (emissive particles).
    bool additive_blend = false;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkColorComponentFlags color_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkDescriptorSetLayout set0_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout set1_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout set2_layout = VK_NULL_HANDLE;

    uint32_t vertex_stride = sizeof(float) * 6;
    std::vector<VertexAttributeDesc> vertex_attributes;

    uint32_t push_constant_size = 0;
    VkShaderStageFlags push_constant_stages = 0;
};

inline GraphicsPipelineConfig withMeshVertexLayout(GraphicsPipelineConfig config) {
    config.vertex_stride = sizeof(float) * 8;
    config.vertex_attributes = {
        {0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)},
        {2, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 6)},
    };
    return config;
}

class GraphicsPipeline {
public:
    GraphicsPipeline(const VulkanContext& context, const GraphicsPipelineConfig& config);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shader_modules_;
};

} // namespace genesis
