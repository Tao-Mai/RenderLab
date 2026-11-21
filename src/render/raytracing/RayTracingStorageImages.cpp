#include "render/raytracing/RayTracingStorageImages.h"

#include "render/device/VulkanContext.h"
#include "render/graph/PassPorts.h"
#include "render/resources/ShaderData.h"

namespace
{
constexpr auto kColorFeatures =
    vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
}

void RayTracingStorageImages::reflect(RenderPassReflection& reflection)
{
    reflection.output("rayImage", PortKind::Image, true);
    pass_ports::outputs<DenoiserInputs>(reflection, true);
}

void RayTracingStorageImages::Outputs::publish(PassResources& ports) const
{
    ports.setOutput("rayImage", rayImage);
    pass_ports::write(ports, denoiser);
}

RayTracingStorageImages::Outputs RayTracingStorageImages::compile(
    RenderGraph& graph, const PassSetup& setup, const ShaderLayout& shader, bool splitLighting
)
{
    images.clear();
    const auto createImage = [&](std::string_view name, vk::FormatFeatureFlags features = {})
    {
        return create(graph, setup, shader.storageImage(name, RenderInterface::kRayTracingPassSet), features);
    };

    Outputs outputs;
    const auto color = createImage("outputImage", kColorFeatures);
    outputs.denoiser.depth = createImage("depthImage");
    outputs.denoiser.worldNormal = createImage("normalImage");
    if (splitLighting)
    {
        outputs.denoiser.direct = color;
        outputs.denoiser.indirect = createImage("indirectImage");
        outputs.denoiser.emission = createImage("emissionImage");
        outputs.denoiser.albedo = createImage("albedoImage");
    }
    else
        outputs.rayImage = color;

    return outputs;
}

RenderGraph::ImageHandle RayTracingStorageImages::create(
    RenderGraph& graph, const PassSetup& setup, const ShaderStorageImage& target,
    vk::FormatFeatureFlags additionalFeatures
)
{
    const auto format = target.format;
    const auto required = vk::FormatFeatureFlagBits::eStorageImage | additionalFeatures;
    const auto features = setup.vulkan.physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures;
    CHECK((features & required) == required, "ray tracing image format {} requires {}",
          vk::to_string(format), vk::to_string(required));

    const auto extent = setup.renderExtent;
    const auto image = graph.write(graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}}));
    graph.use(image, {
        .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .access = vk::AccessFlagBits2::eShaderStorageWrite,
        .layout = vk::ImageLayout::eGeneral,
    });
    images.push_back({target.binding, image});
    return image;
}

void RayTracingStorageImages::appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const
{
    for (const auto& image : images)
        bindings.push_back(image.binding.layout(vk::ShaderStageFlagBits::eRaygenKHR));
}

void RayTracingStorageImages::writeDescriptors(
    RenderGraph& graph, const PassSetup& setup, std::span<const vk::raii::DescriptorSet> sets
) const
{
    std::vector<vk::DescriptorImageInfo> infos(images.size());
    std::vector<vk::WriteDescriptorSet> writes(images.size());
    for (size_t index = 0; index < images.size(); ++index)
    {
        const auto image = images[index].image;
        infos[index] = vk::DescriptorImageInfo{
            .imageView = graph.getImage(image).getOrCreateImageView({
                .viewType = vk::ImageViewType::e2D,
                .format = graph.imageInfo(image).format,
                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
            }),
            .imageLayout = vk::ImageLayout::eGeneral,
        };
        writes[index] = images[index].binding.write({});
        writes[index].pImageInfo = &infos[index];
    }

    for (const auto& set : sets)
    {
        for (auto& write : writes)
            write.dstSet = *set;
        setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
    }
}
