#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include <filesystem>

namespace genesis {

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path);

} // namespace genesis
