#include "renderer/vulkan/ComputePipeline.hpp"

#include "renderer/vulkan/ShaderModule.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <stdexcept>

namespace genesis {

ComputePipeline::ComputePipeline(const VulkanContext& context, const std::filesystem::path& comp_shader,
                                 const VkDescriptorSetLayout set0_layout,
                                 const std::uint32_t push_constant_size) {
    device_ = context.device();
    shader_module_ = loadShaderModule(device_, comp_shader);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader_module_;
    stage.pName = "main";

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set0_layout;

    VkPushConstantRange push{};
    if (push_constant_size > 0) {
        push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push.offset = 0;
        push.size = push_constant_size;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
    }

    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline layout");
    }

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage = stage;
    pipeline_info.layout = layout_;

    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline");
    }
}

ComputePipeline::~ComputePipeline() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
    if (shader_module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, shader_module_, nullptr);
    }
}

} // namespace genesis
