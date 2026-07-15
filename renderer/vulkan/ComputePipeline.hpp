#pragma once

#include <filesystem>
#include <vector>

#include <volk.h>

namespace genesis {

class VulkanContext;

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path);

class ComputePipeline {
public:
    ComputePipeline(const VulkanContext& context, const std::filesystem::path& comp_shader,
                    VkDescriptorSetLayout set0_layout, std::uint32_t push_constant_size = 0);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
};

} // namespace genesis
