#include "render/pass/PointShadowPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/pass/RenderGraph.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"

#include <array>
#include <meta>


PointShadowPass::Output PointShadowPass::setup(RenderGraph& graph)
{
    // 资源缓存
    pointShadowMapSize = context().config->rendererConfig().feature.shadow.pointShadowMapSize;

    shader         = renderContext().shaderManager->getOrLoad("shadow");
    pipelineLayout = renderContext().pipelineManager->layout(PipelineLayoutPreset::SceneOnly);

    pipeline = renderContext().pipelineManager->getOrCreate({
        .shader      = shader,
        .layout      = PipelineLayoutPreset::SceneOnly,
        .state       = PipelineState::preset(RenderMode::Shadow),
        .depthFormat = graph.shadowFormat(),
        .viewMask    = kPointViewMask,
    });


    // usage 由graph推导
    pointShadowMap = graph.createImage(
        {.flags       = vk::ImageCreateFlagBits::eCubeCompatible,
         .imageType   = vk::ImageType::e2D,
         .format      = graph.shadowFormat(),
         .extent      = {pointShadowMapSize, pointShadowMapSize, 1},
         .mipLevels   = 1,
         .arrayLayers = 6 * kMaxPointLightShadow}
    );


    graph.use(
        pointShadowMap,
        {.stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
         .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
         .layout = vk::ImageLayout::eDepthAttachmentOptimal}
    );

    return {pointShadowMap};
}

void PointShadowPass::execute(RenderGraph& graph) const
{
    if (!context().config->rendererConfig().feature.shadow.enable)
        return;

    auto& image = graph.getImage(pointShadowMap);

    auto&            commandBuffer = graph.commands();
    const std::array sceneSets     = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    const auto       lights        = graph.frameData(graph.frameIndex()).lightData();

    for (uint32_t lightIdx = 0; lightIdx < lights.size(); ++lightIdx)
    {
        const LightData& light = lights[lightIdx];
        if (!light.enable() || !light.castShadow() || light.type() != LightComponent::Type::Point)
            continue;

        DCHECK(light.shadowidx < graph.frameData(graph.frameIndex()).pointShadowData().size());

        // TODO 提前算
        const auto& imageView = image.getOrCreateImageView(
            {.viewType         = vk::ImageViewType::e2DArray,
             .format           = graph.shadowFormat(),
             .subresourceRange = vk::ImageSubresourceRange{
                 .aspectMask     = vk::ImageAspectFlagBits::eDepth,
                 .baseMipLevel   = 0,
                 .levelCount     = 1,
                 .baseArrayLayer = light.shadowidx * 6,
                 .layerCount     = 6
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
            .renderArea       = {{0, 0}, {pointShadowMapSize, pointShadowMapSize}},
            .layerCount       = 1,
            .viewMask         = kPointViewMask,
            .pDepthAttachment = &depthAttachment,
        });

        // Multiview resets graphics state at the beginning of each rendering instance.
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            RenderInterface::sceneSet,
            sceneSets,
            {}
        );
        commandBuffer.setViewport(
            0,
            vk::Viewport(
                0.0f,
                0.0f,
                static_cast<float>(pointShadowMapSize),
                static_cast<float>(pointShadowMapSize),
                0.0f,
                1.0f
            )
        );
        commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {pointShadowMapSize, pointShadowMapSize}));

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
