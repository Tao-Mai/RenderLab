#include "render/raytracing/TemporalReprojectionPass.h"
#include "render/FrameData.h"
#include "render/RenderView.h"

#include "render/RenderContext.h"
#include "render/RendererConfig.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuShader.h"

namespace
{
constexpr uint32_t   kWorkgroupSize = 8;
constexpr std::array kHistoryFormats{
    vk::Format::eR32G32B32A32Sfloat,  // RGB mean + sample count.
    vk::Format::eR32Sfloat,           // Positive view-space depth; zero means no surface.
    vk::Format::eR16G16B16A16Sfloat,  // World geometric normal.
    vk::Format::eR32G32Sfloat,        // Luminance first and second raw moments.
};
constexpr auto kHistoryAccess = RenderGraph::ImageAccess{
    .stages = vk::PipelineStageFlagBits2::eComputeShader,
    .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
    .layout = vk::ImageLayout::eGeneral,
};

struct ReprojectionParameters
{
    glm::mat4 previousViewProjection;
    glm::vec4 previousViewport;
    glm::vec4 previousDepthRow;
    glm::vec4 viewport;
    uint32_t  writeLayer;
    uint32_t  historyValid;
    uint32_t  emaStartSampleCount;
    float     emaAlpha;
};
static_assert(sizeof(ReprojectionParameters) == 128);

glm::vec4 linearDepthRow(const glm::mat4& view)
{
    // Only -view.z is needed; keep both camera transforms within 128-byte push constants.
    return -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
}

vk::ImageView imageView(RenderGraph& graph, RenderGraph::ImageHandle image)
{
    const auto& info = graph.imageInfo(image);
    return graph.getImage(image).getOrCreateImageView({
        .viewType         = info.arrayLayers == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray,
        .format           = info.format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = info.arrayLayers,
        },
    });
}
}  // namespace

TemporalReprojectionPass::Output
TemporalReprojectionPass::setup(RenderGraph& graph, const PassSetup& setup, RayTracingPass::Output source)
{
    input              = source;
    extent             = graph.imageInfo(input.rayImage).extent;
    const auto& vulkan = setup.vulkan;
    CHECK(vulkan.supportedFeatures().shaderStorageImageExtendedFormats);
    for (size_t index = 0; index < history.size(); ++index)
    {
        CHECK(
            vulkan.physicalDeviceHandle().getFormatProperties(kHistoryFormats[index]).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eStorageImage
        );
        history[index] = graph.write(graph.createImage({
            .format      = kHistoryFormats[index],
            .extent      = extent,
            .arrayLayers = 2,
        }));
        graph.use(history[index], kHistoryAccess);
    }
    hasHistory = false;
    writeLayer = 0;

    const auto sampled = RenderGraph::ImageAccess{
        .stages = vk::PipelineStageFlagBits2::eComputeShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    graph.use(input.rayImage, sampled);
    graph.use(input.depth, sampled);
    graph.use(input.worldNormal, sampled);
    color = graph.write(graph.createImage({.format = kHdrColorFormat, .extent = extent}));
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
            .layout = vk::ImageLayout::eGeneral,
        }
    );
    if (!*pipeline)
        createPipeline(vulkan);
    return {color, history[3]};
}

void TemporalReprojectionPass::createPipeline(const VulkanContext& vulkan)
{
    CHECK(
        vulkan.physicalDeviceHandle().getQueueFamilyProperties()[vulkan.graphicsQueueFamilyIndex()].queueFlags &
        vk::QueueFlagBits::eCompute
    );
    const auto&                                   device = vulkan.deviceHandle();
    std::array<vk::DescriptorSetLayoutBinding, 8> bindings;
    for (uint32_t binding = 0; binding < bindings.size(); ++binding)
        bindings[binding] = {
            .binding         = binding,
            .descriptorType  = binding < 3 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        };
    auto& renderDevice  = *renderContext().device;
    descriptorSetLayout = renderDevice.createDescriptorSetLayout(bindings);
    const vk::PushConstantRange range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .size       = sizeof(ReprojectionParameters)
    };
    const std::array layouts{&renderDevice.sceneDescriptorSetLayout, &descriptorSetLayout};
    pipelineLayout = renderDevice.getOrCreatePipelineLayout(layouts, std::array{range});
    GpuShader                     shader{device, "shaders/temporal_reprojection.spv"};
    const vk::raii::PipelineCache cache{nullptr};
    pipeline      = vkCheck(device.createComputePipeline(
        cache,
        {
                 .stage  = {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
                 .layout = pipelineLayout,
        }
    ));
    descriptorSet = renderDevice.allocateDescriptorSet(descriptorSetLayout);
}

void TemporalReprojectionPass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const
{
    const std::array
        handles{input.rayImage, input.depth, input.worldNormal, history[0], history[1], history[2], history[3], color};
    std::array<vk::DescriptorImageInfo, 8> images;
    std::array<vk::WriteDescriptorSet, 8>  writes;
    for (uint32_t binding = 0; binding < handles.size(); ++binding)
    {
        images[binding] = {
            .imageView   = imageView(graph, handles[binding]),
            .imageLayout = binding < 3 ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eGeneral,
        };
        writes[binding] = {
            .dstSet          = *descriptorSet,
            .dstBinding      = binding,
            .descriptorCount = 1,
            .descriptorType  = binding < 3 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage,
            .pImageInfo      = &images[binding],
        };
    }
    setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
}

void TemporalReprojectionPass::execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame)
{
    const auto&     settings = frame.setup.config.feature.rayTracing;
    const auto&     viewport = frame.view.viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const auto      currentDepthRow = linearDepthRow(frame.view.uniforms.view);
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    const std::array descriptorSets{frame.data.sceneSetHandle(), *descriptorSet};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSets, {});
    commands.pushConstants<ReprojectionParameters>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        ReprojectionParameters{
            .previousViewProjection = previousViewProjection,
            .previousViewport       = previousViewport,
            .previousDepthRow       = previousDepthRow,
            .viewport               = currentViewport,
            .writeLayer             = writeLayer,
            .historyValid           = hasHistory ? 1u : 0u,
            .emaStartSampleCount    = settings.emaStartSampleCount,
            .emaAlpha               = settings.emaAlpha,
        }
    );
    commands.dispatch(
        (extent.width + kWorkgroupSize - 1) / kWorkgroupSize,
        (extent.height + kWorkgroupSize - 1) / kWorkgroupSize,
        1
    );
    previousViewProjection = frame.view.uniforms.viewProjection;
    previousViewport       = currentViewport;
    previousDepthRow       = currentDepthRow;
    writeLayer ^= 1u;
    hasHistory = true;
}
