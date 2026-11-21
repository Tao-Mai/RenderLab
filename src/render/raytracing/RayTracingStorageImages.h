#pragma once

#include "render/RenderContext.h"
#include "render/device/VulkanResources.h"
#include "render/resources/ShaderLayout.h"

#include <span>
#include <string_view>
#include <vector>

[[nodiscard]] ShaderBinding<vk::DescriptorType::eStorageImage> getRayTracingStorageImageBinding(
    const VulkanContext& vulkan,
    const ShaderLayout& shader,
    std::string_view name,
    vk::Format format,
    vk::FormatFeatureFlags additionalFeatures = {}
);

class RayTracingStorageImages
{
public:
    void
         add(VulkanResources&             cache,
             const VulkanContext&           vulkan,
             const ShaderLayout&        shader,
             std::string_view           name,
             VulkanResources::ImageHandle image,
             vk::FormatFeatureFlags     additionalFeatures = {});
    void appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const;
    void writeDescriptors(VulkanResources& cache, std::span<const vk::raii::DescriptorSet> sets) const;

private:
    struct Binding
    {
        ShaderBinding<vk::DescriptorType::eStorageImage> binding;
        VulkanResources::ImageHandle                       image;
    };
    std::vector<Binding> images;
};
