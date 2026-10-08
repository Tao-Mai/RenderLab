//
// Created by 24500 on 2026/10/8.
//

#include "render/feature/shadow/DirectionalShadowPass.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/feature/RenderGraph.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"

#include <array>

DirectionalShadowPass::Output DirectionalShadowPass::setup(RenderGraph& graph)
{
    const auto& cascade  = context().config->rendererConfig().feature.shadow.cascade;
    cascadeShadowMapSize = cascade.shadowMapSize;
    cascadeCount         = cascade.count;

    CHECK(cascadeCount > 0 && cascadeCount <= kMaxDirectionalShadowCascades);
    const auto& limits = graph.vulkan().properties().limits;
    CHECK(
        cascadeShadowMapSize <= limits.maxImageDimension2D && cascadeShadowMapSize <= limits.maxFramebufferWidth &&
        cascadeShadowMapSize <= limits.maxFramebufferHeight &&
        kMaxDirectionalLightShadow * cascadeCount <= limits.maxImageArrayLayers
    );
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features =
        graph.vulkan().physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures;
    CHECK((features & kRequiredFeatures) == kRequiredFeatures);

    shader         = renderContext().shaderManager->getOrLoad("shadow", ShaderVariant::DirectionalShadow);
    pipelineLayout = renderContext().pipelineManager->layout(PipelineLayoutPreset::SceneOnly);

    pipeline = renderContext().pipelineManager->getOrCreate({
        .shader      = shader,
        .layout      = PipelineLayoutPreset::SceneOnly,
        .state       = PipelineState::preset(RenderMode::Shadow),
        .depthFormat = vk::Format::eD32Sfloat,
    });


    // usage 由graph推导
    cascadeShadowMap = graph.createImage(
        {.imageType   = vk::ImageType::e2D,
         .format      = vk::Format::eD32Sfloat,
         .extent      = {cascadeShadowMapSize, cascadeShadowMapSize, 1},
         .mipLevels   = 1,
         .arrayLayers = kMaxDirectionalLightShadow * cascadeCount}
    );
    cascadeShadowMap = graph.write(cascadeShadowMap);


    graph.use(
        cascadeShadowMap,
        {.stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
         .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
         .layout = vk::ImageLayout::eDepthAttachmentOptimal}
    );

    return {cascadeShadowMap};
}

void DirectionalShadowPass::execute(RenderGraph& graph) const
{
    if (!context().config->rendererConfig().feature.shadow.enable)
        return;

    auto& image = graph.getImage(cascadeShadowMap);

    auto&            commandBuffer = graph.commands();
    const std::array sceneSets     = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    const auto       lights        = graph.frameData(graph.frameIndex()).lightData();

    for (uint32_t lightIdx = 0; lightIdx < lights.size(); ++lightIdx)
    {
        const LightData& light = lights[lightIdx];
        if (!light.enable() || !light.castShadow() || light.type() != LightComponent::Type::Directional)
            continue;

        DCHECK(light.shadowidx < graph.frameData(graph.frameIndex()).directionalShadowData().size());
        DCHECK(cascadeCount == graph.frameData(graph.frameIndex()).lightUniforms().cascadeCount);

        for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            const auto& imageView = image.getOrCreateImageView(
                {.viewType         = vk::ImageViewType::e2D,
                 .format           = vk::Format::eD32Sfloat,
                 .subresourceRange = vk::ImageSubresourceRange{
                     .aspectMask     = vk::ImageAspectFlagBits::eDepth,
                     .baseMipLevel   = 0,
                     .levelCount     = 1,
                     .baseArrayLayer = light.shadowidx * cascadeCount + cascadeIndex,
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
                .renderArea       = {{0, 0}, {cascadeShadowMapSize, cascadeShadowMapSize}},
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
                    static_cast<float>(cascadeShadowMapSize),
                    static_cast<float>(cascadeShadowMapSize),
                    0.0f,
                    1.0f
                )
            );
            commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {cascadeShadowMapSize, cascadeShadowMapSize}));

            for (const DrawItem& item : graph.renderData().drawItems)
            {
                const ShadowPushConstants push{.model = item.model, .lightIdx = lightIdx, .cascadeIndex = cascadeIndex};
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
}
