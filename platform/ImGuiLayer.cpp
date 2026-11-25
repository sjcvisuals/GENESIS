#include "platform/ImGuiLayer.hpp"
#include "platform/EditorPaths.hpp"

#include "renderer/vulkan/Swapchain.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace genesis {

namespace {

void checkVulkanResult(VkResult err) {
    if (err != VK_SUCCESS) {
        throw std::runtime_error("ImGui Vulkan error: VkResult " + std::to_string(static_cast<int>(err)));
    }
}

float displayContentScale(SDL_Window* window) {
    if (!window) {
        return 1.0f;
    }
    const float scale = SDL_GetWindowDisplayScale(window);
    if (scale > 0.01f) {
        return scale;
    }
    return 1.0f;
}

bool tryAddFont(ImGuiIO& io, const char* path, float size_px) {
    if (!path || !std::filesystem::exists(path)) {
        return false;
    }
    return io.Fonts->AddFontFromFileTTF(path, size_px) != nullptr;
}

} // namespace

ImGuiLayer::ImGuiLayer(SDL_Window* window, const VulkanContext& context, Swapchain& swapchain,
                       float ui_scale)
    : window_(window), device_(context.device()) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    // Default null: host tools and short-lived players avoid CRT heap asserts when
    // ImGui writes imgui.ini during DestroyContext. Editor calls enableLayoutPersistence().
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    loadFonts(ui_scale);

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

void ImGuiLayer::loadFonts(float ui_scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Do not bake display DPI into the atlas — ImGui SDL3 sets DisplayFramebufferScale.
    // Only the user/editor ui_scale enlarges the base 16 px face.
    const float dpi = displayContentScale(window_);
    const float scale = std::clamp(ui_scale > 0.01f ? ui_scale : 1.0f, 1.0f, 2.5f);
    const float size_px = 16.0f * scale;

    // Linux cloud image → Inter; Windows → Segoe UI; else Noto / Proggy default.
    constexpr const char* kCandidates[] = {
        "/usr/share/fonts/truetype/macos/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansDisplay-Regular.ttf",
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\SegoeUI.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
#endif
    };
    constexpr const char* kMediumCandidates[] = {
        "/usr/share/fonts/truetype/macos/Inter-Medium.ttf",
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\seguisb.ttf", // Segoe UI Semibold
#endif
    };
    constexpr const char* kSemiCandidates[] = {
        "/usr/share/fonts/truetype/macos/Inter-SemiBold.ttf",
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\seguisb.ttf",
#endif
    };

    bool loaded = false;
    for (const char* path : kCandidates) {
        if (tryAddFont(io, path, size_px)) {
            loaded = true;
            for (const char* medium : kMediumCandidates) {
                tryAddFont(io, medium, size_px);
            }
            for (const char* semi : kSemiCandidates) {
                tryAddFont(io, semi, size_px);
            }
            break;
        }
    }
    if (!loaded) {
        io.Fonts->AddFontDefault();
        // ProggyClean ~13px — scale up so tools stay readable when Inter/Segoe missing.
        io.FontGlobalScale = scale;
        spdlog::warn("No system UI font found; using ImGui default (FontGlobalScale={:.2f}, dpi={:.2f})",
                     io.FontGlobalScale, dpi);
    } else {
        io.FontGlobalScale = 1.0f;
        spdlog::info("ImGui UI font {:.1f}px (ui_scale={:.2f}, display_scale={:.2f})", size_px, scale,
                     dpi);
    }

    if (!io.Fonts->Fonts.empty()) {
        io.FontDefault = io.Fonts->Fonts[0];
    }
}

void ImGuiLayer::enableLayoutPersistence() {
    ImGui::GetIO().IniFilename = editorLayoutIniCString();
}

ImGuiLayer::~ImGuiLayer() {
    vkDeviceWaitIdle(device_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    // Never let DestroyContext write imgui.ini — Debug CRT often asserts (_CrtIsValidHeapPointer)
    // on that path. Layout is saved during NewFrame when WantSaveIniSettings is set.
    if (ImGui::GetCurrentContext()) {
        ImGui::GetIO().IniFilename = nullptr;
    }
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
