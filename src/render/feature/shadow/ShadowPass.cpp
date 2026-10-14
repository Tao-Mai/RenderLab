#include "render/feature/shadow/ShadowPass.h"
#include "render/feature/SceneInterface.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/ShaderManager.h"
#include "render/Visibility.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"

#include <array>

ShadowPass::Output ShadowPass::setup(RenderGraph& graph, const PassSetup& setup)
{
    const auto& shadow = context().config->rendererConfig().feature.shadow;
    cascadeCount       = shadow.cascade.count;

    CHECK(cascadeCount > 0 && cascadeCount <= kMaxDirectionalShadowCascades);
    const auto& limits = setup.vulkan.properties().limits;
    CHECK(
        shadow.cascade.shadowMapSize <= limits.maxImageDimension2D &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferWidth &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferHeight &&
        kMaxDirectionalLightShadow * cascadeCount <= limits.maxImageArrayLayers
    );
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features =
        setup.vulkan.physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures;
    CHECK((features & kRequiredFeatures) == kRequiredFeatures);

    auto& pipelines = *renderContext().pipelineManager;
    pipelineLayout  = SceneInterface::shadowLayout(*renderContext().descriptors, *renderContext().pipelineManager);
    const auto createTarget =
        [&](RenderGraph::ImageCreateInfo info, int shadowType, uint32_t viewMask = 0, uint32_t viewLayerCount = 1)
    {
        CHECK(info.extent.width > 0 && info.extent.height > 0);
        CHECK(
            info.extent.width <= limits.maxImageDimension2D && info.extent.height <= limits.maxImageDimension2D &&
            info.extent.width <= limits.maxFramebufferWidth && info.extent.height <= limits.maxFramebufferHeight &&
            info.arrayLayers <= limits.maxImageArrayLayers
        );
        if (info.flags & vk::ImageCreateFlagBits::eCubeCompatible)
            CHECK(info.extent.width <= limits.maxImageDimensionCube);
        const auto shader   = renderContext().shaderManager->getOrLoad("shadow", {{"SHADOW_TYPE", shadowType}});
        const auto pipeline = pipelines.getOrCreate({
            .shader = shader,
            .layout = pipelineLayout,
            .state =
                PipelineState{
                    .vertexLayout = VertexLayoutPreset::PositionOnly,
                    .depthClamp   = shadowType == static_cast<int>(GpuLightType::Directional),
                },
            .depthFormat = vk::Format::eD32Sfloat,
            .viewMask    = viewMask,
        });

        auto image = graph.createImage(info);
        image      = graph.write(image);
        graph.use(
            image,
            {
                .stages =
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                .layout = vk::ImageLayout::eDepthAttachmentOptimal,
            }
        );

        return ShadowTarget{
            .image          = image,
            .pipeline       = pipeline,
            .mapSize        = info.extent.width,
            .viewMask       = viewMask,
            .viewLayerCount = viewLayerCount,
        };
    };

    point = createTarget(
        {
            .flags       = vk::ImageCreateFlagBits::eCubeCompatible,
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.pointShadowMapSize, shadow.pointShadowMapSize, 1},
            .arrayLayers = kPointFaceCount * kMaxPointLightShadow,
        },
        0,
        kPointViewMask,
        kPointFaceCount
    );
    spot = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.spotShadowMapSize, shadow.spotShadowMapSize, 1},
            .arrayLayers = kMaxSpotLightShadow,
        },
        static_cast<int>(GpuLightType::Spot)
    );
    rectArea = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.rectAreaShadowMapSize, shadow.rectAreaShadowMapSize, 1},
            .arrayLayers = kMaxRectAreaLightShadow,
        },
        2
    );
    directional = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.cascade.shadowMapSize, shadow.cascade.shadowMapSize, 1},
            .arrayLayers = kMaxDirectionalLightShadow * cascadeCount,
        },
        1
    );

    return {point.image, spot.image, rectArea.image, directional.image};
}

void ShadowPass::execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const
{
    const auto& frame  = execution.data;
    const auto  lights = frame.lightData();
    for (uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
    {
        const LightData& light = lights[lightIndex];
        if (!light.enable() || !light.castShadow())
            continue;

        switch (light.type())
        {
            case GpuLightType::Point:
                {
                    DCHECK(light.shadowidx < frame.pointShadowData().size());
                    recordShadow(graph, commandBuffer, execution, point, lightIndex, light.shadowidx * kPointFaceCount);
                    break;
                }
            case GpuLightType::Spot:
                {
                    DCHECK(light.shadowidx < frame.spotShadowData().size());
                    recordShadow(graph, commandBuffer, execution, spot, lightIndex, light.shadowidx);
                    break;
                }
            case GpuLightType::RectArea:
                {
                    DCHECK(light.shadowidx < frame.rectAreaShadowData().size());
                    recordShadow(graph, commandBuffer, execution, rectArea, lightIndex, light.shadowidx);
                    break;
                }
            case GpuLightType::Directional:
                {
                    DCHECK(light.shadowidx < frame.directionalShadowData().size());
                    DCHECK(cascadeCount == frame.lightUniforms().cascadeCount);
                    for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                    {
                        recordShadow(
                            graph,
                            commandBuffer,
                            execution,
                            directional,
                            lightIndex,
                            light.shadowidx * cascadeCount + cascadeIndex,
                            cascadeIndex
                        );
                    }
                    break;
                }
            default:
                break;
        }
    }
}

void ShadowPass::recordShadow(
    RenderGraph&        graph,
    CommandRecorder&    commandBuffer,
    const PassFrame&    execution,
    const ShadowTarget& target,
    uint32_t            lightIndex,
    uint32_t            baseArrayLayer,
    uint32_t            cascadeIndex
) const
{
    // TODO 提前算
    const auto imageView =
        graph.getImage(target.image, execution.frameIndex)
            .getOrCreateImageView({
                .viewType         = target.viewLayerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D,
                .format           = vk::Format::eD32Sfloat,
                .subresourceRange = {
                    .aspectMask     = vk::ImageAspectFlagBits::eDepth,
                    .levelCount     = 1,
                    .baseArrayLayer = baseArrayLayer,
                    .layerCount     = target.viewLayerCount,
                },
            });
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = imageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };

    commandBuffer.beginRendering({
        .renderArea       = {{0, 0}, {target.mapSize, target.mapSize}},
        .layerCount       = 1,
        .viewMask         = target.viewMask,
        .pDepthAttachment = &depthAttachment,
    });

    // Multiview resets graphics state at the beginning of each rendering instance.
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, target.pipeline);
    const std::array sceneSets = {execution.data.sceneSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        sceneSets,
        {}
    );
    commandBuffer.setViewport(
        0,
        vk::Viewport(0.0f, 0.0f, static_cast<float>(target.mapSize), static_cast<float>(target.mapSize), 0.0f, 1.0f)
    );
    commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {target.mapSize, target.mapSize}));

    const auto& light      = execution.data.lightData()[lightIndex];
    const auto  visibility = RenderVisibility::shadowMask(light.type(), light.shadowidx, cascadeIndex);
    for (const DrawItem& item : execution.renderData.drawItems)
    {
        if (!(item.visibilityMask & visibility))
            continue;

        const ShadowPushConstants push{.model = item.model, .lightIdx = lightIndex, .cascadeIndex = cascadeIndex};
        commandBuffer.pushConstants<ShadowPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            push
        );
        item.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
    }

    commandBuffer.endRendering();
}
