#include "render/pass/ShadowPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/pass/RenderGraph.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"

#include <array>
#include <meta>

void ShadowPass::init(RenderGraph& graph)
{
    shader = graph.shaders().getOrLoad("shadow");

    // 用Nearest，因为插值的话得到的是无意义的深度
    sampler = vkCheck(graph.vulkan().deviceHandle().createSampler({
        .magFilter    = vk::Filter::eNearest,
        .minFilter    = vk::Filter::eNearest,
        .mipmapMode   = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod       = 0.0f,
    }));
}

void ShadowPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass(
        "shadow",
        inputCount,
        outputCount,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); }
    );
}

void ShadowPass::setupPass(RenderGraph& graph)
{
    pointShadowMapSize = context().config->rendererConfig().feature.shadow.pointShadowMapSize;
    const auto& limits = graph.vulkan().properties().limits;
    CHECK(
        pointShadowMapSize > 0 && pointShadowMapSize <= limits.maxImageDimensionCube &&
            pointShadowMapSize <= limits.maxImageDimension2D && pointShadowMapSize <= limits.maxFramebufferWidth &&
            pointShadowMapSize <= limits.maxFramebufferHeight,
        "point shadow map size {} exceeds device limits",
        pointShadowMapSize
    );

    CHECK(
        kMaxPointLightShadow <= graph.vulkan().properties().limits.maxImageArrayLayers / 6,
        "point shadow cubemap array exceeds maxImageArrayLayers"
    );
    const RenderGraph::ResourceDesc depthUse{
        .format      = graph.shadowFormat(),
        .extent      = {pointShadowMapSize, pointShadowMapSize, 1},
        .arrayLayers = kMaxPointLightShadow * 6,
        .imageFlags  = vk::ImageCreateFlagBits::eCubeCompatible,
        .viewType    = vk::ImageViewType::eCubeArray,
        .aspect      = vk::ImageAspectFlagBits::eDepth,
        .imageUsage  = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .stages      = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .layout = vk::ImageLayout::eDepthAttachmentOptimal,
    };
    graph.createResource(inputSlots[Depth], depthUse);
    graph.bindOutput(outputSlots[ShadowMap], inputSlots[Depth]);

    pipeline = graph.pipelines().getOrCreate({
        .shader      = shader,
        .layout      = PipelineLayoutPreset::SceneOnly,
        .state       = PipelineState::preset(RenderMode::Shadow),
        .depthFormat = graph.shadowFormat(),
        .viewMask    = pointViewMask,
    });

    pipelineLayout = graph.pipelines().layout(PipelineLayoutPreset::SceneOnly);
}

void ShadowPass::bindResources(RenderGraph& graph)
{
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const vk::DescriptorImageInfo imageInfo{
            .imageView   = graph.imageView(inputSlots[Depth], frameIndex),
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo samplerInfo{.sampler = *sampler};
        const std::array              writes = {
            vk::WriteDescriptorSet{
                             .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                             .dstBinding      = RenderInterface::shadowImageBinding,
                             .descriptorCount = 1,
                             .descriptorType  = vk::DescriptorType::eSampledImage,
                             .pImageInfo      = &imageInfo,
            },
            vk::WriteDescriptorSet{
                             .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                             .dstBinding      = RenderInterface::shadowSamplerBinding,
                             .descriptorCount = 1,
                             .descriptorType  = vk::DescriptorType::eSampler,
                             .pImageInfo      = &samplerInfo,
            },
        };
        graph.vulkan().deviceHandle().updateDescriptorSets(writes, {});
    }
}

void ShadowPass::executePass(RenderGraph& graph) const
{
    if (!context().config->rendererConfig().feature.shadow.enable)
        return;

    auto&            commandBuffer = graph.commands();
    const std::array sceneSets     = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    const auto       lights        = graph.frameData(graph.frameIndex()).lightData();

    for (uint32_t lightIdx = 0; lightIdx < lights.size(); ++lightIdx)
    {
        const LightData& light = lights[lightIdx];
        if (!light.enable() || !light.castShadow() || light.type() != LightComponent::Type::Point)
            continue;

        DCHECK(light.shadowidx < graph.frameData(graph.frameIndex()).pointShadowData().size());

        const vk::RenderingAttachmentInfo depthAttachment{
            .imageView   = graph.layerView(inputSlots[Depth], light.shadowidx * 6, 6),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
        };
        commandBuffer.beginRendering({
            .renderArea       = {{0, 0}, {pointShadowMapSize, pointShadowMapSize}},
            .layerCount       = 1,
            .viewMask         = pointViewMask,
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
