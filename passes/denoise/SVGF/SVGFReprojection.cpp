#include "render/device/ResourceBarriers.h"
#include "passes/denoise/SVGF/SVGFReprojection.h"

#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/RenderView.h"

#include "render/RenderContext.h"
#include "render/device/VulkanContext.h"

namespace
{
constexpr uint32_t   kInputCount    = 4;
constexpr uint32_t   kBindingCount  = 10;
constexpr auto kHistoryAccess = VulkanResources::ImageAccess{
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
    uint32_t  historyLength;
    float     alpha;
};
static_assert(sizeof(ReprojectionParameters) == 128);

glm::vec4 linearDepthRow(const glm::mat4& view)
{
    // Only -view.z is needed; keep both camera transforms within 128-byte push constants.
    return -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
}
}  // namespace

SVGFReprojection::Output SVGFReprojection::compile(
    const Falcor::RenderContext& context,
    const DenoiserInputs& source,
    const std::array<VulkanResources::ImageHandle, kHistoryCount>& history,
    const std::array<VulkanResources::ImageHandle, kLightingLayerCount>& outputs
)
{
    auto& cache = *context.vulkanResources;
    const auto& vulkan = *context.vulkan;
    accesses.clear();
    historySource      = source.color;
    extent             = cache.imageInfo(source.color).extent;
    CHECK(vulkan.supportedFeatures().shaderStorageImageExtendedFormats);
    const auto& [previousGeometry, previousLighting, previousMoments, previousHistoryLength] = history;
    for (const auto image : history)
    {
        const auto format = cache.imageInfo(image).format;
        CHECK(
            vulkan.physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eStorageImage
        );
        auto access = kHistoryAccess;
        if (image == previousLighting)
            access.access = vk::AccessFlagBits2::eShaderStorageRead;
        accesses.add(image, access);
    }
    hasHistory = false;
    writeLayer = 0;

    constexpr auto kSampled = vulkan_access::sampled(vk::PipelineStageFlagBits2::eComputeShader);
    for (const auto image : {source.color, source.depth, source.worldNormal, source.albedo})
        accesses.add(image, kSampled);
    const Output output{
        .direct          = outputs[kDirectLightingLayer],
        .indirect        = outputs[kIndirectLightingLayer],
        .lightingHistory = previousLighting,
    };
    constexpr VulkanResources::ImageAccess kStorageWrite{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::ImageLayout::eGeneral
    };
    accesses.add(output.direct, kStorageWrite);
    accesses.add(output.indirect, kStorageWrite);

    accesses.declare(cache);

    if (!*pipeline)
        createPipeline(context);

    const std::array handles{
        source.color,
        source.depth,
        source.worldNormal,
        source.albedo,
        previousGeometry,
        previousLighting,
        previousMoments,
        previousHistoryLength,
        output.direct,
        output.indirect,
    };
    for (uint32_t binding = 0; binding < handles.size(); ++binding)
    {
        const auto image   = handles[binding];
        const bool sampled = binding < kInputCount;
        const auto type    = sampled ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage;
        const auto view =
            cache.imageInfo(image).arrayLayers == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray;
        cache.writeDescriptor(*descriptorSet, binding, image, type, view);
    }
    return output;
}

void SVGFReprojection::createPipeline(const Falcor::RenderContext& context)
{
    const auto& vulkan = *context.vulkan;
    CHECK(
        vulkan.physicalDeviceHandle().getQueueFamilyProperties()[vulkan.graphicsQueueFamilyIndex()].queueFlags &
        vk::QueueFlagBits::eCompute
    );
    std::array<vk::DescriptorSetLayoutBinding, kBindingCount> bindings;
    for (uint32_t binding = 0; binding < bindings.size(); ++binding)
        bindings[binding] = {
            .binding = binding,
            .descriptorType =
                binding < kInputCount ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        };
    auto& device        = *context.getDevice();
    descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    const vk::PushConstantRange range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .size       = sizeof(ReprojectionParameters)
    };
    const std::array layouts{&device.sceneDescriptorSetLayout, &descriptorSetLayout};
    pipelineLayout = device.getOrCreatePipelineLayout(layouts, std::array{range});
    const auto shader = context.shaderManager->getOrLoad("svgf_reprojection");
    pipeline = device.createComputePipeline(shader, pipelineLayout);
    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
}

void SVGFReprojection::execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame)
{
    accesses.transition(cache, commands);
    const auto      historyRevision = cache.imageHistoryRevision(historySource);
    const auto&     viewport = frame.view->viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const auto      currentDepthRow = linearDepthRow(frame.view->uniforms.view);
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    const std::array descriptorSets{frame.frameData->sceneSetHandle(), *descriptorSet};
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
            .historyValid           = hasHistory && previousHistoryRevision == historyRevision ? 1u : 0u,
            .historyLength          = settings.historyLength,
            .alpha                  = settings.alpha,
        }
    );
    pipeline.dispatch(commands, {extent.width, extent.height, 1});
    previousViewProjection = frame.view->uniforms.viewProjection;
    previousViewport       = currentViewport;
    previousDepthRow       = currentDepthRow;
    previousHistoryRevision = historyRevision;
    writeLayer ^= 1u;
    hasHistory = true;
}
