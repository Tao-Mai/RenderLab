#include "render/feature/shadow/ShadowPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/ShaderManager.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"

#include <array>

ShadowPass::Output ShadowPass::setup(RenderGraph& graph)
{
    const auto& shadow = context().config->rendererConfig().feature.shadow;
    cascadeCount       = shadow.cascade.count;

    CHECK(cascadeCount > 0 && cascadeCount <= kMaxDirectionalShadowCascades);
    const auto& limits = graph.vulkan().properties().limits;
    CHECK(
        shadow.cascade.shadowMapSize <= limits.maxImageDimension2D &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferWidth &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferHeight &&
        kMaxDirectionalLightShadow * cascadeCount <= limits.maxImageArrayLayers
    );
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features =
        graph.vulkan().physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures;
    CHECK((features & kRequiredFeatures) == kRequiredFeatures);

    auto& pipelines         = *renderContext().pipelineManager;
    pipelineLayout          = pipelines.layout(PipelineLayoutPreset::SceneOnly);
    const auto createTarget = [&](RenderGraph::ImageCreateInfo info,
                                  ShaderVariant                variant,
                                  uint32_t                     viewMask       = 0,
                                  uint32_t                     viewLayerCount = 1)
    {
        const auto shader   = renderContext().shaderManager->getOrLoad("shadow", variant);
        const auto pipeline = pipelines.getOrCreate({
            .shader      = shader,
            .layout      = PipelineLayoutPreset::SceneOnly,
            .state       = PipelineState::preset(RenderMode::Shadow),
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
        ShaderVariant::Default,
        kPointViewMask,
        kPointFaceCount
    );
    rectArea = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.rectAreaShadowMapSize, shadow.rectAreaShadowMapSize, 1},
            .arrayLayers = kMaxRectAreaLightShadow,
        },
        ShaderVariant::RectAreaShadow
    );
    directional = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.cascade.shadowMapSize, shadow.cascade.shadowMapSize, 1},
            .arrayLayers = kMaxDirectionalLightShadow * cascadeCount,
        },
        ShaderVariant::DirectionalShadow
    );

    return {point.image, rectArea.image, directional.image};
}

void ShadowPass::execute(RenderGraph& graph) const
{
    const auto& frame  = graph.frameData(graph.frameIndex());
    const auto  lights = frame.lightData();
    for (uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
    {
        const LightData& light = lights[lightIndex];
        if (!light.enable() || !light.castShadow())
            continue;

        switch (light.type())
        {
            case LightComponent::Type::Point:
                {
                    DCHECK(light.shadowidx < frame.pointShadowData().size());
                    recordShadow(graph, point, lightIndex, light.shadowidx * kPointFaceCount);
                    break;
                }
            case LightComponent::Type::RectArea:
                {
                    DCHECK(light.shadowidx < frame.rectAreaShadowData().size());
                    recordShadow(graph, rectArea, lightIndex, light.shadowidx);
                    break;
                }
            case LightComponent::Type::Directional:
                {
                    DCHECK(light.shadowidx < frame.directionalShadowData().size());
                    DCHECK(cascadeCount == frame.lightUniforms().cascadeCount);
                    for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                    {
                        recordShadow(
                            graph,
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
    RenderGraph& graph, const ShadowTarget& target, uint32_t lightIndex, uint32_t baseArrayLayer, uint32_t cascadeIndex
) const
{
    // TODO 提前算
    const auto imageView =
        graph.getImage(target.image)
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

    auto& commandBuffer = graph.commands();
    commandBuffer.beginRendering({
        .renderArea       = {{0, 0}, {target.mapSize, target.mapSize}},
        .layerCount       = 1,
        .viewMask         = target.viewMask,
        .pDepthAttachment = &depthAttachment,
    });

    // Multiview resets graphics state at the beginning of each rendering instance.
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, target.pipeline);
    const std::array sceneSets = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
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

    for (const DrawItem& item : graph.renderData().drawItems)
    {
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
