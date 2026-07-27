#include "renderer/vulkan/VulkanContext.hpp"

#include "renderer/vulkan/VulkanInterop.hpp"

#include <SDL3/SDL_vulkan.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>

namespace genesis {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("Vulkan: {}", callback_data->pMessage);
    } else {
        spdlog::debug("Vulkan: {}", callback_data->pMessage);
    }
    return VK_FALSE;
}

bool validationLayersSupported() {
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

    constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

bool configIsHeadless(const VulkanContextConfig& config) {
    return config.window == nullptr;
}

VulkanContext::VulkanContext(const VulkanContextConfig& config) {
    headless_ = configIsHeadless(config);

    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize volk");
    }

    validation_enabled_ = config.enable_validation && validationLayersSupported();
    if (config.enable_validation && !validation_enabled_) {
        spdlog::warn("Vulkan validation layers requested but not available");
    }

    createInstance(config);
    volkLoadInstance(instance_);

    if (validation_enabled_) {
        setupDebugMessenger();
    }

    if (!headless_) {
        createSurface(config.window);
    }
    pickPhysicalDevice(config.adapter_luid);
    createLogicalDevice(config);

    volkLoadDevice(device_);
    if (headless_) {
        loadVulkanWin32Interop(device_);
    }
    spdlog::info("Vulkan device initialized ({})", headless_ ? "headless" : "windowed");
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (debug_messenger_ != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) {
            destroy(instance_, debug_messenger_, nullptr);
        }
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void VulkanContext::createInstance(const VulkanContextConfig& config) {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "GENESIS";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "GENESIS";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions;
    if (headless_) {
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    } else {
        uint32_t sdl_extension_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
        if (!sdl_extensions) {
            throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
        }
        extensions.insert(extensions.end(), sdl_extensions, sdl_extensions + sdl_extension_count);
    }
    extensions.insert(extensions.end(), config.extra_instance_extensions.begin(),
                     config.extra_instance_extensions.end());

    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
    if (validation_enabled_) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = validation_layers;
    }

    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void VulkanContext::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = debugCallback;

    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!create_fn || create_fn(instance_, &create_info, nullptr, &debug_messenger_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up Vulkan debug messenger");
    }
}

void VulkanContext::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("Failed to create Vulkan surface: ") + SDL_GetError());
    }
}

void VulkanContext::pickPhysicalDevice(std::uint64_t adapter_luid) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        throw std::runtime_error("No Vulkan-capable GPUs found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    auto device_luid = [](VkPhysicalDevice device) -> std::uint64_t {
        VkPhysicalDeviceIDProperties id_props{};
        id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &id_props;

        vkGetPhysicalDeviceProperties2(device, &props2);

        std::uint64_t luid = 0;
        std::memcpy(&luid, id_props.deviceLUID, VK_LUID_SIZE);
        return luid;
    };

    for (const auto device : devices) {
        if (adapter_luid != 0 && device_luid(device) != adapter_luid) {
            continue;
        }

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

        uint32_t graphics_family = UINT32_MAX;
        uint32_t present_family = UINT32_MAX;

        for (uint32_t i = 0; i < queue_family_count; ++i) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphics_family = i;
            }
            if (!headless_ && surface_ != VK_NULL_HANDLE) {
                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_support);
                if (present_support) {
                    present_family = i;
                }
            }
        }

        if (graphics_family == UINT32_MAX) {
            continue;
        }

        if (headless_) {
            present_family = graphics_family;
        } else if (present_family == UINT32_MAX) {
            continue;
        }

        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available.data());

        auto has_extension = [&](const char* name) {
            return std::any_of(available.begin(), available.end(), [&](const VkExtensionProperties& prop) {
                return std::strcmp(prop.extensionName, name) == 0;
            });
        };

        if (!headless_ && !has_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        // OpenPBR materials use a 256-byte push block (widely supported; Vulkan min is 128).
        if (props.limits.maxPushConstantsSize < 256) {
            spdlog::warn("Skipping GPU {} — maxPushConstantsSize {} < 256", props.deviceName,
                         props.limits.maxPushConstantsSize);
            continue;
        }

        physical_device_ = device;
        graphics_queue_family_ = graphics_family;
        present_queue_family_ = present_family;

        spdlog::info("Selected GPU: {} (push constants {} bytes)", props.deviceName,
                     props.limits.maxPushConstantsSize);
        return;
    }

    throw std::runtime_error("Failed to find a suitable Vulkan GPU");
}

void VulkanContext::createLogicalDevice(const VulkanContextConfig& config) {
    std::set<uint32_t> unique_queue_families = {graphics_queue_family_, present_queue_family_};
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    float queue_priority = 1.0f;

    for (uint32_t family : unique_queue_families) {
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_info);
    }

    if (headless_) {
#ifdef _WIN32
        device_extensions_ = {
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
            VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        };
#else
        device_extensions_ = {
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        };
#endif
    } else {
        device_extensions_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    }
    for (const char* ext : config.extra_device_extensions) {
        device_extensions_.push_back(ext);
    }

    VkPhysicalDeviceFeatures device_features{};
    device_features.samplerAnisotropy = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures indexing_features{};
    indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    indexing_features.descriptorBindingPartiallyBound = VK_TRUE;
    indexing_features.runtimeDescriptorArray = VK_TRUE;
    indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &indexing_features;
    features2.features = device_features;

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.pEnabledFeatures = nullptr;
    create_info.pNext = &features2;
    create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions_.size());
    create_info.ppEnabledExtensionNames = device_extensions_.data();

    if (validation_enabled_) {
        const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = validation_layers;
    }

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_queue_family_, 0, &present_queue_);
}

uint32_t VulkanContext::findMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

void VulkanContext::immediateSubmit(const std::function<void(VkCommandBuffer)>& record) const {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = graphics_queue_family_;
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create immediate command pool");
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = command_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &alloc, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        throw std::runtime_error("Failed to allocate immediate command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        throw std::runtime_error("Failed to begin immediate command buffer");
    }

    record(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        throw std::runtime_error("Failed to end immediate command buffer");
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_, &fence_info, nullptr, &fence) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        throw std::runtime_error("Failed to create immediate fence");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    if (vkQueueSubmit(graphics_queue_, 1, &submit, fence) != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        vkDestroyCommandPool(device_, command_pool, nullptr);
        throw std::runtime_error("Failed to submit immediate command buffer");
    }

    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device_, fence, nullptr);
    vkDestroyCommandPool(device_, command_pool, nullptr);
}

} // namespace genesis
