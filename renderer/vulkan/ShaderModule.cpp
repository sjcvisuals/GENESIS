#include "renderer/vulkan/ShaderModule.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace genesis {

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }

    const size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<char> code(file_size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(file_size));

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + path.string());
    }
    return module;
}

} // namespace genesis
