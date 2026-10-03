#include "render/pass/ShadowPass.h"

#include "core/Logger.h"
#include "scene/component/LightComponent.h"
#include "scene/component/TransformComponent.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/pass/RenderGraph.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr std::array faceDirections = {
    glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f},
    glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
};

constexpr std::array faceUp = {
    glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
    glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f},
};
}

void ShadowPass::init(RenderGraph& graph)
{
    shader = graph.shaders().getOrLoad("shadow");
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
    std::tie(inputSlots, outputSlots) = graph.registerPass("shadow", InputCount, OutputCount,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); });
}

void ShadowPass::setupPass(RenderGraph& graph)
{
    const RenderGraph::ResourceDesc depthUse{
        .format = graph.shadowFormat(),
        .extent = {mapSize, mapSize, 1},
        .arrayLayers = 6,
        .imageFlags = vk::ImageCreateFlagBits::eCubeCompatible,
        .viewType = vk::ImageViewType::eCube,
        .aspect = vk::ImageAspectFlagBits::eDepth,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .layout = vk::ImageLayout::eDepthAttachmentOptimal,
    };
    graph.createResource(inputSlots[Depth], depthUse);
    graph.bindOutput(outputSlots[ShadowMap], inputSlots[Depth], {
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
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet = graph.frameContext(frameIndex).sceneSetHandle(),
                .dstBinding = RenderInterface::shadowImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &imageInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet = graph.frameContext(frameIndex).sceneSetHandle(),
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
    const auto lights = graph.frameContext(graph.frameIndex()).lightData();
    // The first SSBO element is the scene's primary light, matching shadow.slang.
    const LightData* light = lights.empty() ? nullptr : &lights.front();
    const bool castShadow = light != nullptr && light->flags.y != 0 && light->flags.z != 0 &&
        light->flags.x == static_cast<uint32_t>(LightComponent::Type::Point);
    glm::mat4 projection{1.0f};
    if (castShadow)
    {
        CHECK(std::isfinite(light->positionRange.w) && light->positionRange.w > 0.0f,
            "shadow-casting point light requires a positive finite range");
        const float nearPlane = std::min(0.1f, light->positionRange.w * 0.1f);
        // Face up vectors account for Vulkan's downward-growing image coordinates.
        projection = glm::perspective(std::numbers::pi_v<float> * 0.5f,
            1.0f, nearPlane, light->positionRange.w);
    }

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sceneSets = {graph.frameContext(graph.frameIndex()).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipelineLayout, RenderInterface::sceneSet, sceneSets, {});
    commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f,
        static_cast<float>(mapSize), static_cast<float>(mapSize), 0.0f, 1.0f));
    commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {mapSize, mapSize}));

    for (uint32_t face = 0; face < 6; ++face)
    {
        const vk::RenderingAttachmentInfo depthAttachment{
            .imageView = graph.layerView(inputSlots[Depth], face),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearDepthStencilValue(1.0f, 0),
        };
        commandBuffer.beginRendering({
            .renderArea = {{0, 0}, {mapSize, mapSize}},
            .layerCount = 1,
            .pDepthAttachment = &depthAttachment,
        });

        // No shadow-casting point light still produces a cleared, valid cubemap.
        if (castShadow)
        {
            const glm::vec3 lightPosition = light->positionRange;
            const glm::mat4 view = glm::lookAt(lightPosition,
                lightPosition + faceDirections[face], faceUp[face]);
            const glm::mat4 viewProjection = projection * view;
            for (const SceneRenderItem& item : graph.scene().renderItems())
            {
                const auto* objectTransform = item.actor->getComponent<TransformComponent>();
                DCHECK(objectTransform);

                const ShadowPushConstants push{
                    .model = objectTransform->matrix(),
                    .viewProjection = viewProjection,
                };
                commandBuffer.pushConstants<ShadowPushConstants>(pipelineLayout,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
                item.mesh->bind(commandBuffer);
                for (const Submesh& submesh : item.mesh->submeshes())
                {
                    commandBuffer.drawIndexed(submesh.indexCount, 1,
                        submesh.firstIndex, 0, 0);
                }
            }
        }
        commandBuffer.endRendering();
    }
}
