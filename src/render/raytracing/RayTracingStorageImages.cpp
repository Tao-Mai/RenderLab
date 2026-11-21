#include "render/raytracing/RayTracingStorageImages.h"

#include "core/Logger.h"
#include "render/device/VulkanContext.h"
#include "render/resources/ShaderData.h"

ShaderBinding<vk::DescriptorType::eStorageImage> getRayTracingStorageImageBinding(
    const VulkanContext& vulkan, const ShaderLayout& shader, std::string_view name, vk::Format format,
    vk::FormatFeatureFlags additionalFeatures
)
{
    const auto target = shader.storageImage(name, RenderInterface::kRayTracingPassSet);
    CHECK(
        format == target.format,
        "resource format {} does not match shader image '{}' ({})",
        vk::to_string(format),
        name,
        vk::to_string(target.format)
    );
    const auto required = vk::FormatFeatureFlagBits::eStorageImage | additionalFeatures;
    const auto features = vulkan.physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures;
    CHECK((features & required) == required, "ray tracing image format {} requires {}",
          vk::to_string(format), vk::to_string(required));

    return target.binding;
}

void RayTracingStorageImages::add(
    VulkanResources& cache, const VulkanContext& vulkan, const ShaderLayout& shader, std::string_view name,
    VulkanResources::ImageHandle image, vk::FormatFeatureFlags additionalFeatures
)
{
    const auto binding = getRayTracingStorageImageBinding(
        vulkan, shader, name, cache.imageInfo(image).format, additionalFeatures
    );
    images.push_back({binding, image});
}

void RayTracingStorageImages::appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const
{
    for (const auto& image : images)
        bindings.push_back(image.binding.layout(vk::ShaderStageFlagBits::eRaygenKHR));
}

void RayTracingStorageImages::writeDescriptors(
    VulkanResources& cache, std::span<const vk::raii::DescriptorSet> sets
) const
{
    for (const auto& image : images)
    {
        const auto view = cache.imageInfo(image.image).arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
        for (const auto& set : sets)
            cache.writeDescriptor(*set, image.binding.index, image.image, vk::DescriptorType::eStorageImage, view);
    }
}
