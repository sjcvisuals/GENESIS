#include "platform/ImGuiLayer.hpp"
#include "platform/EditorPaths.hpp"

#include "renderer/vulkan/Swapchain.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

namespace genesis {

namespace {

void checkVulkanResult(VkResult err) {
    if (err != VK_SUCCESS) {
        throw std::runtime_error("ImGui Vulkan error: VkResult " + std::to_string(static_cast<int>(err)));
    }
}

} // namespace

ImGuiLayer::ImGuiLayer(SDL_Window* window, const VulkanContext& context, Swapchain& swapchain)
    : window_(window), device_(context.device()) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    // Default null: host tools and short-lived players avoid CRT heap asserts when
    // ImGui writes imgui.ini during DestroyContext. Editor calls enableLayoutPersistence().
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
    }

    createDescriptorPool(context);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_2;
    init_info.Instance = context.instance();
    init_info.PhysicalDevice = context.physicalDevice();
    init_info.Device = context.device();
    init_info.QueueFamily = context.graphicsQueueFamily();
    init_info.Queue = context.graphicsQueue();
    init_info.DescriptorPool = descriptor_pool_;
    init_info.MinImageCount = static_cast<uint32_t>(swapchain.images().size());
    init_info.ImageCount = static_cast<uint32_t>(swapchain.images().size());
    init_info.PipelineInfoMain.RenderPass = swapchain.renderPass();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = false;
    init_info.CheckVkResultFn = checkVulkanResult;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }

    // Font texture is uploaded automatically on the first ImGui_ImplVulkan_NewFrame().
}

void ImGuiLayer::enableLayoutPersistence() {
    ImGui::GetIO().IniFilename = editorLayoutIniCString();
}

ImGuiLayer::~ImGuiLayer() {
    vkDeviceWaitIdle(device_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    }
}

void ImGuiLayer::createDescriptorPool(const VulkanContext& context) {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(context.device(), &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame(VkCommandBuffer command_buffer) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

void ImGuiLayer::onResize() {
    ImGui_ImplVulkan_SetMinImageCount(2);
}

} // namespace genesis
