#include "particles/FluidGpuGrid.hpp"

#include "platform/Paths.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace genesis {

namespace {

struct AdvectPush {
    std::int32_t grid_size[2];
    float dt;
    float dissipation;
    std::int32_t advect_density;
};

struct DivergencePush {
    std::int32_t grid_size[2];
    float cell_size;
};

struct JacobiPush {
    std::int32_t grid_size[2];
};

struct GradientPush {
    std::int32_t grid_size[2];
    float cell_size;
};

struct SplatPush {
    std::int32_t grid_size[2];
    float point[2];
    float velocity[2];
    float radius;
    float density;
    float add_density;
};

struct PresentPush {
    std::int32_t grid_size[2];
    // GLSL push constants align vec4 to 16 bytes after ivec2 — match that padding.
    std::int32_t _pad_align_vec4[2]{};
    float dye_color[4];
};
static_assert(sizeof(PresentPush) == 32, "PresentPush must match fluid_present.comp push layout");

std::uint32_t nextPowerOfTwo(std::uint32_t value) {
    if (value < 64) {
        return 64;
    }
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value++;
    return std::min(value, 512u);
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                  VkAccessFlags src_access, VkAccessFlags dst_access) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
                         src_access ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         dst_access ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

FluidGpuGrid::FluidGpuGrid(const VulkanContext& context) : context_(context), device_(context.device()) {
    createPipelines();
}

FluidGpuGrid::~FluidGpuGrid() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    advect_pipeline_.reset();
    divergence_pipeline_.reset();
    jacobi_pipeline_.reset();
    gradient_pipeline_.reset();
    splat_pipeline_.reset();
    present_pipeline_.reset();
    display_texture_.reset();
    destroyImages();
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    }
}

FluidGpuGrid::StorageImage FluidGpuGrid::createImage(const std::uint32_t size, const VkFormat format) {
    StorageImage out{};
    out.format = format;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {size, size, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &info, nullptr, &out.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid simulation image");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, out.image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex =
        context_.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &alloc, nullptr, &out.memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate fluid simulation image memory");
    }
    vkBindImageMemory(device_, out.image, out.memory, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view_info, nullptr, &out.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid simulation image view");
    }
    return out;
}

void FluidGpuGrid::destroyImage(StorageImage& image) {
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image.view, nullptr);
        image.view = VK_NULL_HANDLE;
    }
    if (image.image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image.image, nullptr);
        image.image = VK_NULL_HANDLE;
    }
    if (image.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, image.memory, nullptr);
        image.memory = VK_NULL_HANDLE;
    }
}

void FluidGpuGrid::destroyImages() {
    for (StorageImage& image : velocity_) {
        destroyImage(image);
    }
    for (StorageImage& image : density_) {
        destroyImage(image);
    }
    for (StorageImage& image : pressure_) {
        destroyImage(image);
    }
    destroyImage(divergence_);
    grid_size_ = 0;
}

void FluidGpuGrid::createPipelines() {
    VkDescriptorSetLayoutBinding bindings[8]{};
    for (std::uint32_t i = 0; i < 8; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 8;
    layout_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_size.descriptorCount = 8;
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &set_layout_;
    if (vkAllocateDescriptorSets(device_, &alloc_info, &descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate fluid descriptor set");
    }

    const std::filesystem::path shader_dir = resolveShaderDirectory();
    advect_pipeline_ = std::make_unique<ComputePipeline>(context_, shader_dir / "fluid_advect.comp.spv",
                                                         set_layout_, sizeof(AdvectPush));
    divergence_pipeline_ = std::make_unique<ComputePipeline>(
        context_, shader_dir / "fluid_divergence.comp.spv", set_layout_, sizeof(DivergencePush));
    jacobi_pipeline_ = std::make_unique<ComputePipeline>(context_, shader_dir / "fluid_jacobi.comp.spv",
                                                         set_layout_, sizeof(JacobiPush));
    gradient_pipeline_ = std::make_unique<ComputePipeline>(
        context_, shader_dir / "fluid_gradient.comp.spv", set_layout_, sizeof(GradientPush));
    splat_pipeline_ =
        std::make_unique<ComputePipeline>(context_, shader_dir / "fluid_splat.comp.spv", set_layout_, sizeof(SplatPush));
    present_pipeline_ = std::make_unique<ComputePipeline>(context_, shader_dir / "fluid_present.comp.spv",
                                                          set_layout_, sizeof(PresentPush));
}

void FluidGpuGrid::ensureResolution(const std::uint32_t resolution) {
    const std::uint32_t size = nextPowerOfTwo(resolution);
    if (size == grid_size_ && display_texture_) {
        return;
    }

    destroyImages();
    grid_size_ = size;
    velocity_[0] = createImage(size, VK_FORMAT_R16G16_SFLOAT);
    velocity_[1] = createImage(size, VK_FORMAT_R16G16_SFLOAT);
    density_[0] = createImage(size, VK_FORMAT_R16_SFLOAT);
    density_[1] = createImage(size, VK_FORMAT_R16_SFLOAT);
    pressure_[0] = createImage(size, VK_FORMAT_R16_SFLOAT);
    pressure_[1] = createImage(size, VK_FORMAT_R16_SFLOAT);
    divergence_ = createImage(size, VK_FORMAT_R16_SFLOAT);
    display_texture_ = std::make_unique<GpuTexture>(GpuTexture::createFluidDisplayTarget(context_, size, size));
    display_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    velocity_index_ = 0;
    density_index_ = 0;
    pressure_index_ = 0;
    velocity_readback_.assign(static_cast<std::size_t>(size) * size * 2, 0.0f);

    context_.immediateSubmit([&](VkCommandBuffer cmd) {
        for (StorageImage* image :
             {&velocity_[0], &velocity_[1], &density_[0], &density_[1], &pressure_[0], &pressure_[1], &divergence_}) {
            imageBarrier(cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                         VK_ACCESS_SHADER_WRITE_BIT);
        }
        imageBarrier(cmd, display_texture_->nativeImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                     VK_ACCESS_SHADER_WRITE_BIT);
        display_layout_ = VK_IMAGE_LAYOUT_GENERAL;
    });

    stats_.grid_resolution = size;
}

void FluidGpuGrid::bindSimDescriptors() {
    const StorageImage& vel_r = velocity_[static_cast<std::size_t>(velocity_index_)];
    const StorageImage& vel_w = velocity_[static_cast<std::size_t>(1 - velocity_index_)];
    const StorageImage& den_r = density_[static_cast<std::size_t>(density_index_)];
    const StorageImage& den_w = density_[static_cast<std::size_t>(1 - density_index_)];
    const StorageImage& pres_r = pressure_[static_cast<std::size_t>(pressure_index_)];
    const StorageImage& pres_w = pressure_[static_cast<std::size_t>(1 - pressure_index_)];

    VkDescriptorImageInfo infos[8]{};
    auto store = [&](std::uint32_t binding, const StorageImage& image, VkImageLayout layout) {
        infos[binding].imageView = image.view;
        infos[binding].imageLayout = layout;
    };
    store(0, vel_r, VK_IMAGE_LAYOUT_GENERAL);
    store(1, vel_w, VK_IMAGE_LAYOUT_GENERAL);
    store(2, den_r, VK_IMAGE_LAYOUT_GENERAL);
    store(3, den_w, VK_IMAGE_LAYOUT_GENERAL);
    store(4, pres_r, VK_IMAGE_LAYOUT_GENERAL);
    store(5, pres_w, VK_IMAGE_LAYOUT_GENERAL);
    store(6, divergence_, VK_IMAGE_LAYOUT_GENERAL);
    infos[7].imageView = display_texture_->view();
    infos[7].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[8]{};
    for (std::uint32_t i = 0; i < 8; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_, 8, writes, 0, nullptr);
}

void FluidGpuGrid::dispatchPass(VkCommandBuffer cmd, ComputePipeline& pipeline, const std::uint32_t size,
                                const void* push_data, const std::uint32_t push_size) {
    const std::uint32_t groups = (size + 7) / 8;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1, &descriptor_set_, 0,
                            nullptr);
    vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push_data);
    vkCmdDispatch(cmd, groups, groups, 1);

    // Ensure subsequent compute passes see storage-image writes from this dispatch.
    VkMemoryBarrier mem{};
    mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mem.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &mem, 0, nullptr, 0, nullptr);
}

void FluidGpuGrid::swapVelocity() {
    velocity_index_ = 1 - velocity_index_;
}

void FluidGpuGrid::swapDensity() {
    density_index_ = 1 - density_index_;
}

void FluidGpuGrid::injectSplat(VkCommandBuffer cmd, const glm::vec2 uv, const glm::vec2 velocity,
                               const float radius, const float density) {
    SplatPush splat{};
    splat.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    splat.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    splat.point[0] = uv.x;
    splat.point[1] = uv.y;
    splat.velocity[0] = velocity.x;
    splat.velocity[1] = velocity.y;
    splat.radius = radius;
    splat.density = density;
    splat.add_density = 1.0f;

    StorageImage& vel = velocity_[static_cast<std::size_t>(velocity_index_)];
    StorageImage& den = density_[static_cast<std::size_t>(density_index_)];
    VkDescriptorImageInfo infos[8]{};
    infos[0].imageView = vel.view;
    infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[1].imageView = vel.view;
    infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[2].imageView = den.view;
    infos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[3].imageView = den.view;
    infos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[4]{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
    dispatchPass(cmd, *splat_pipeline_, grid_size_, &splat, sizeof(splat));
    bindSimDescriptors();
}

void FluidGpuGrid::simulate(VkCommandBuffer cmd, const FluidVolume& volume, const float dt_seconds) {
    if (!volume.enabled) {
        return;
    }

    ensureResolution(volume.grid_resolution);
    if (!display_texture_) {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    bindSimDescriptors();

    sim_time_ += dt_seconds;

    const auto applySource = [&](const bool enabled, glm::vec2 uv, glm::vec2 velocity, float radius,
                                 float density, const float phase) {
        if (!enabled) {
            return;
        }
        if (volume.animate_sources) {
            uv.x += 0.06f * std::sin(sim_time_ * 0.82f + phase);
            uv.y += 0.04f * std::sin(sim_time_ * 1.05f + phase * 1.7f);
            velocity.x += 0.08f * std::sin(sim_time_ * 0.58f + phase * 0.5f);
            velocity.y += 0.05f * std::cos(sim_time_ * 0.71f + phase);
            // Burst the ink so dye forms discrete rising/falling blobs instead of a continuous plume.
            const float pulse = 0.5f + 0.5f * std::sin(sim_time_ * 1.15f + phase * 1.3f);
            const float gate = glm::smoothstep(0.42f, 0.78f, pulse);
            density *= 0.15f + 0.85f * gate;
            radius *= 0.75f + 0.35f * gate;
            velocity.y *= 0.55f + 0.75f * gate;
        }
        uv.x = glm::clamp(uv.x, 0.05f, 0.95f);
        uv.y = glm::clamp(uv.y, 0.05f, 0.95f);
        injectSplat(cmd, uv, velocity, radius, density);
    };

    applySource(volume.source_enabled, volume.source_uv, volume.source_velocity, volume.source_radius,
                volume.source_density, 0.0f);
    applySource(volume.source2_enabled, volume.source2_uv, volume.source2_velocity, volume.source2_radius,
                volume.source2_density, 2.1f);

    AdvectPush advect_vel{};
    advect_vel.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    advect_vel.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    advect_vel.dt = dt_seconds;
    advect_vel.dissipation = volume.velocity_dissipation;
    advect_vel.advect_density = 0;
    dispatchPass(cmd, *advect_pipeline_, grid_size_, &advect_vel, sizeof(advect_vel));
    swapVelocity();
    bindSimDescriptors();

    DivergencePush div_push{};
    div_push.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    div_push.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    div_push.cell_size = 1.0f / static_cast<float>(grid_size_);
    dispatchPass(cmd, *divergence_pipeline_, grid_size_, &div_push, sizeof(div_push));

    JacobiPush jacobi{};
    jacobi.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    jacobi.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    const std::uint32_t iterations = std::max(volume.pressure_iterations, 4u);
    for (std::uint32_t i = 0; i < iterations; ++i) {
        bindSimDescriptors();
        dispatchPass(cmd, *jacobi_pipeline_, grid_size_, &jacobi, sizeof(jacobi));
        pressure_index_ = 1 - pressure_index_;
    }

    GradientPush grad{};
    grad.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    grad.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    grad.cell_size = 1.0f / static_cast<float>(grid_size_);
    bindSimDescriptors();
    dispatchPass(cmd, *gradient_pipeline_, grid_size_, &grad, sizeof(grad));
    swapVelocity();
    bindSimDescriptors();

    AdvectPush advect_den{};
    advect_den.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    advect_den.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    advect_den.dt = dt_seconds;
    advect_den.dissipation = volume.density_dissipation;
    advect_den.advect_density = 1;
    dispatchPass(cmd, *advect_pipeline_, grid_size_, &advect_den, sizeof(advect_den));
    swapDensity();
    bindSimDescriptors();

    // Present wrote SHADER_READ_ONLY last frame — return to GENERAL before writing again.
    imageBarrier(cmd, display_texture_->nativeImage(),
                 display_layout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED
                                                              : display_layout_,
                 VK_IMAGE_LAYOUT_GENERAL,
                 display_layout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT : 0,
                 VK_ACCESS_SHADER_WRITE_BIT);
    display_layout_ = VK_IMAGE_LAYOUT_GENERAL;
    bindSimDescriptors();

    PresentPush present{};
    present.grid_size[0] = static_cast<std::int32_t>(grid_size_);
    present.grid_size[1] = static_cast<std::int32_t>(grid_size_);
    present.dye_color[0] = volume.dye_color.r;
    present.dye_color[1] = volume.dye_color.g;
    present.dye_color[2] = volume.dye_color.b;
    present.dye_color[3] = volume.dye_color.a;
    dispatchPass(cmd, *present_pipeline_, grid_size_, &present, sizeof(present));

    imageBarrier(cmd, display_texture_->nativeImage(), VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    display_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    readbackVelocityField();

    const auto end = std::chrono::steady_clock::now();
    stats_.pressure_iterations = iterations;
    stats_.last_step_ms =
        static_cast<float>(std::chrono::duration<double, std::milli>(end - start).count());
}

void FluidGpuGrid::readbackVelocityField() {
    // GPU→CPU velocity readback is not wired yet. Keep the buffer allocated so the
    // coupling path can fall back to a synthetic circulation field below.
    (void)velocity_readback_;
}

glm::vec2 FluidGpuGrid::sampleVelocityUv(const float u, const float v) const {
    if (grid_size_ == 0) {
        return glm::vec2(0.0f);
    }

    // Prefer real readback when any non-zero sample exists; otherwise approximate
    // lava-lamp circulation so particle coupling still feels intuitive.
    if (!velocity_readback_.empty()) {
        const float x = glm::clamp(u, 0.0f, 1.0f) * static_cast<float>(grid_size_ - 1);
        const float y = glm::clamp(v, 0.0f, 1.0f) * static_cast<float>(grid_size_ - 1);
        const std::size_t ix = static_cast<std::size_t>(x);
        const std::size_t iy = static_cast<std::size_t>(y);
        const std::size_t idx = (iy * grid_size_ + ix) * 2;
        if (idx + 1 < velocity_readback_.size()) {
            const glm::vec2 sampled(velocity_readback_[idx], velocity_readback_[idx + 1]);
            if (sampled.x != 0.0f || sampled.y != 0.0f) {
                return sampled;
            }
        }
    }

    // Synthetic UV-space field: rising plume in the center, descending return flow
    // near the sides (matches the default dual-source lava lamp).
    const float dx = u - 0.5f;
    const float plume = (1.0f - std::min(1.0f, std::abs(dx) * 3.0f)) * 0.55f;
    const float return_flow = (std::abs(dx) > 0.22f) ? -0.28f : 0.0f;
    const float vertical = plume + return_flow + (0.5f - v) * 0.08f;
    const float horizontal = -dx * 0.45f;
    return glm::vec2(horizontal, vertical);
}

} // namespace genesis
