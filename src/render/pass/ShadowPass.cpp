#include "render/pass/ShadowPass.h"

#include "core/Logger.h"
#include "scene/component/LightComponent.h"
#include "scene/system/RenderSystem.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/pass/RenderGraph.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <array>

void ShadowPass::init(RenderGraph& graph)
{
    shader  = graph.shaders().getOrLoad("shadow");
    sampler = vkCheck(graph.vulkan().deviceHandle().createSampler({
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
        }));
}

void ShadowPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass("shadow",
                                                           InputCount,
                                                           OutputCount,
                                                           [this](RenderGraph& g) { setupPass(g); },
                                                           [this](RenderGraph& g) { executePass(g); });
}

void ShadowPass::setupPass(RenderGraph& graph)
{
    // Reserve one cube per light so type/enable/castShadow changes do not require a rebuild.
    const size_t capacity = std::max<size_t>(graph.renderData().lightObjects.size(), 1);
    CHECK(capacity <= graph.vulkan().properties().limits.maxImageArrayLayers / 6,
        "point shadow cubemap array exceeds maxImageArrayLayers");
    const RenderGraph::ResourceDesc depthUse{
        .format = graph.shadowFormat(),
        .extent = {mapSize, mapSize, 1},
        .arrayLayers = static_cast<uint32_t>(capacity) * 6,
        .imageFlags = vk::ImageCreateFlagBits::eCubeCompatible,
        .viewType = vk::ImageViewType::eCubeArray,
        .aspect = vk::ImageAspectFlagBits::eDepth,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
        vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .layout = vk::ImageLayout::eDepthAttachmentOptimal,
    };
    graph.createResource(inputSlots[Depth], depthUse);
    graph.bindOutput(outputSlots[ShadowMap],
                     inputSlots[Depth],
                     {
                         .imageUsage = vk::ImageUsageFlagBits::eSampled,
                         .stages = vk::PipelineStageFlagBits2::eFragmentShader,
                         .access = vk::AccessFlagBits2::eShaderSampledRead,
                         .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
                     });

    pipeline = graph.pipelines().getOrCreate({
        .shader = shader,
        .layout = PipelineLayoutPreset::SceneOnly,
        .state = PipelineState::preset(RenderMode::Shadow),
        .depthFormat = graph.shadowFormat(),
        .viewMask = pointViewMask,
    });
    pipelineLayout = graph.pipelines().layout(PipelineLayoutPreset::SceneOnly);
}

void ShadowPass::bindResources(RenderGraph& graph)
{
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const vk::DescriptorImageInfo imageInfo{
            .imageView = graph.imageView(inputSlots[Depth], frameIndex),
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo samplerInfo{.sampler = *sampler};
        const std::array              writes = {
            vk::WriteDescriptorSet{
                .dstSet = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding = RenderInterface::shadowImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &imageInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding = RenderInterface::shadowSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &samplerInfo,
            },
        };
        graph.vulkan().deviceHandle().updateDescriptorSets(writes, {});
    }
}

void ShadowPass::executePass(RenderGraph& graph) const
{
    auto& commandBuffer = graph.commands();
    const std::array sceneSets = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    const auto lights = graph.frameData(graph.frameIndex()).lightData();

    const auto beginPointShadow = [&](uint32_t shadowidx)
    {
        const vk::RenderingAttachmentInfo depthAttachment{
            .imageView = graph.layerView(inputSlots[Depth], shadowidx * 6, 6),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearDepthStencilValue(1.0f, 0),
        };
        commandBuffer.beginRendering({
            .renderArea = {{0, 0}, {mapSize, mapSize}},
            .layerCount = 1,
            .viewMask = pointViewMask,
            .pDepthAttachment = &depthAttachment,
        });
    };

    bool rendered = false;
    for (uint32_t lightidx = 0; lightidx < lights.size(); ++lightidx)
    {
        const LightData& light = lights[lightidx];
        if (!light.enable() || !light.castShadow() || light.type() != LightComponent::Type::Point)
            continue;

        DCHECK(light.shadowidx < graph.frameData(graph.frameIndex()).pointShadowData().size());
        beginPointShadow(light.shadowidx);

        // Multiview resets graphics state at the beginning of each rendering instance.
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            pipelineLayout, RenderInterface::sceneSet, sceneSets, {});
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f,
            static_cast<float>(mapSize), static_cast<float>(mapSize), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {mapSize, mapSize}));

        for (const DrawItem& item : graph.renderData().drawItems)
        {
            const ShadowPushConstants push{.model = item.model, .lightidx = lightidx};
            commandBuffer.pushConstants<ShadowPushConstants>(pipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            item.mesh->bind(commandBuffer);
            commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
        }
        commandBuffer.endRendering();
        rendered = true;
    }

    if (!rendered)
    {
        beginPointShadow(0);
        commandBuffer.endRendering();
    }
}
