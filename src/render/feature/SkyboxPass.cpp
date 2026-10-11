#include "render/feature/SkyboxPass.h"
#include "render/feature/SceneInterface.h"

#include "asset/AssetManager.h"
#include "asset/EnvironmentMapAsset.h"
#include "asset/TextureAsset.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/VulkanContext.h"
#include "render/feature/SceneDraw.h"
#include "render/resource/GpuTexture.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <array>

SkyboxPass::Output
SkyboxPass::setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle, RenderGraph::ImageHandle depthHandle)
{
    color = graph.write(colorHandle);
    depth = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);

    refreshBindings(graph);

    auto& pipelines = *renderContext().pipelineManager;
    pipelineLayout  = SceneInterface::skyboxLayout(*renderContext().descriptors, *renderContext().pipelineManager);
    pipeline        = pipelines.getOrCreate({
               .shader = renderContext().shaderManager->getOrLoad("skybox"),
               .layout = pipelineLayout,
               .state =
            PipelineState{
                       .vertexLayout = VertexLayoutPreset::None,
                       .depthCompare = vk::CompareOp::eLessOrEqual,
                       .depthWrite   = false
            },
               .colorFormats = {graph.imageInfo(color).format},
               .depthFormat  = vk::Format::eD32Sfloat,
    });
    return {color};
}

void SkyboxPass::execute(RenderGraph& graph) const
{
    const auto viewport      = SceneDraw::beginRendering(graph, std::array{color}, depth, vk::AttachmentLoadOp::eLoad);
    auto&      commandBuffer = graph.commands();
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sceneSets = {graph.frameData(graph.frameIndex()).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        sceneSets,
        {}
    );
    const SkyboxPushConstants parameters{
        .viewport = {
            static_cast<float>(viewport.offset.x),
            static_cast<float>(viewport.offset.y),
            static_cast<float>(viewport.extent.width),
            static_cast<float>(viewport.extent.height),
        },
    };
    commandBuffer.pushConstants<SkyboxPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commandBuffer.draw(3, 1, 0, 0);
    commandBuffer.endRendering();
}

void SkyboxPass::refreshBindings(RenderGraph& graph)
{
    const auto& environmentId = context().config->rendererConfig().environmentMap;
    DCHECK(environmentId);
    const auto& environment = context().assetManager->get<EnvironmentMapAsset>(*environmentId);
    const auto& binding     = environment.radiance;
    CHECK(!binding.textureID.empty() && !binding.samplerID.empty());
    const auto& asset = context().assetManager->get<TextureAsset>(binding.textureID);
    CHECK(asset.layout == ImageLayout::Cubemap, "environment '{}' requires a radiance cubemap", environment.id);
    auto& resources = *renderContext().resources;
    radianceTexture = resources.texture(binding.textureID);
    const vk::DescriptorImageInfo radianceImage{
        .imageView   = radianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo radianceSampler{.sampler = resources.sampler(binding.samplerID)};
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const auto       set    = graph.frameData(frameIndex).sceneSetHandle();
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet          = set,
                .dstBinding      = RenderInterface::kRadianceImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &radianceImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = set,
                .dstBinding      = RenderInterface::kRadianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &radianceSampler,
            },
        };
        graph.vulkan().deviceHandle().updateDescriptorSets(writes, {});
    }
}
