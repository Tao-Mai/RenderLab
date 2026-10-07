#include "render/feature/shadow/RectAreaLightShadowPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/RenderGraph.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"

#include <array>


RectAreaLightShadowPass::Output RectAreaLightShadowPass::setup(RenderGraph& graph)
{
    constexpr auto kRequiredFeatures =
        vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage |
        vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features =
        graph.vulkan().physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures;
    CHECK((features & kRequiredFeatures) == kRequiredFeatures);

    // 资源缓存
    rectAreaShadowMapSize = context().config->rendererConfig().feature.shadow.rectAreaShadowMapSize;

    shader         = renderContext().shaderManager->getOrLoad("shadow", ShaderVariant::RectAreaShadow);
    pipelineLayout = renderContext().pipelineManager->layout(PipelineLayoutPreset::SceneOnly);

    pipeline = renderContext().pipelineManager->getOrCreate({
        .shader      = shader,
        .layout      = PipelineLayoutPreset::SceneOnly,
        .state       = PipelineState::preset(RenderMode::Shadow),
        .depthFormat = vk::Format::eD32Sfloat,
    });


    // usage 由graph推导
    rectAreaLightShadowMap = graph.createImage(
        {.imageType   = vk::ImageType::e2D,
         .format      = vk::Format::eD32Sfloat,
         .extent      = {rectAreaShadowMapSize, rectAreaShadowMapSize, 1},
         .mipLevels   = 1,
         .arrayLayers = kMaxRectAreaLightShadow}
    );
    rectAreaLightShadowMap = graph.write(rectAreaLightShadowMap);


    graph.use(
        rectAreaLightShadowMap,
        {.stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
         .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
         .layout = vk::ImageLayout::eDepthAttachmentOptimal}
    );

    return {rectAreaLightShadowMap};
}

void RectAreaLightShadowPass::execute(RenderGraph& graph) const
{
    if (!context().config->rendererConfig().feature.shadow.enable)
        return;

    auto& image = graph.getImage(rectAreaLightShadowMap);

    auto&            commandBuffer = graph.commands();
    const std::array sceneSets     = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    const auto       lights        = graph.frameData(graph.frameIndex()).lightData();

    for (uint32_t lightIdx = 0; lightIdx < lights.size(); ++lightIdx)
    {
        const LightData& light = lights[lightIdx];
        if (!light.enable() || !light.castShadow() || light.type() != LightComponent::Type::RectArea)
            continue;

        DCHECK(light.shadowidx < graph.frameData(graph.frameIndex()).rectAreaShadowData().size());

        const auto& imageView = image.getOrCreateImageView(
            {.viewType         = vk::ImageViewType::e2D,
             .format           = vk::Format::eD32Sfloat,
             .subresourceRange = vk::ImageSubresourceRange{
                 .aspectMask     = vk::ImageAspectFlagBits::eDepth,
                 .baseMipLevel   = 0,
                 .levelCount     = 1,
                 .baseArrayLayer = light.shadowidx,
                 .layerCount     = 1
             }}
        );

        const vk::RenderingAttachmentInfo depthAttachment{
            .imageView   = imageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
        };


        commandBuffer.beginRendering({
            .renderArea       = {{0, 0}, {rectAreaShadowMapSize, rectAreaShadowMapSize}},
            .layerCount       = 1,
            .pDepthAttachment = &depthAttachment,
        });

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            RenderInterface::kSceneSet,
            sceneSets,
            {}
        );
        commandBuffer.setViewport(
            0,
            vk::Viewport(
                0.0f,
                0.0f,
                static_cast<float>(rectAreaShadowMapSize),
                static_cast<float>(rectAreaShadowMapSize),
                0.0f,
                1.0f
            )
        );
        commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {rectAreaShadowMapSize, rectAreaShadowMapSize}));

        for (const DrawItem& item : graph.renderData().drawItems)
        {
            const ShadowPushConstants push{.model = item.model, .lightIdx = lightIdx};
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
}
