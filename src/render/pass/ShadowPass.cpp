#include "render/pass/ShadowPass.h"

#include "core/Logger.h"
#include "ecs/Light.h"
#include "ecs/Transform.h"
#include "render/PipelineManager.h"
#include "render/device/FrameContext.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
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

void ShadowPass::init(const VulkanContext& vulkan, PipelineManager& pipelines,
    ShaderHandle shader, const std::array<FrameContext, maxFramesInFlight>& targetFrames)
{
    frames = &targetFrames;
    const auto& physicalDevice = vulkan.physicalDeviceHandle();
    const auto& device = vulkan.deviceHandle();
    constexpr auto requiredFeatures = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage |
        vk::FormatFeatureFlagBits::eTransferDst;
    for (const vk::Format candidate : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm})
    {
        if ((physicalDevice.getFormatProperties(candidate).optimalTilingFeatures &
                requiredFeatures) == requiredFeatures)
        {
            depthFormat = candidate;
            break;
        }
    }
    CHECK(depthFormat != vk::Format::eUndefined,
        "point shadow map requires a sampled depth attachment format");

    const vk::ImageCreateInfo imageInfo{
        .flags = vk::ImageCreateFlagBits::eCubeCompatible,
        .imageType = vk::ImageType::e2D,
        .format = depthFormat,
        .extent = {mapSize, mapSize, 1},
        .mipLevels = 1,
        .arrayLayers = 6,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    for (Target& target : targets)
    {
        target.image = vkCheck(device.createImage(imageInfo));
        const vk::MemoryRequirements requirements = target.image.getMemoryRequirements();
        target.memory = vkCheck(device.allocateMemory({
            .allocationSize = requirements.size,
            .memoryTypeIndex = vulkan_memory::findType(physicalDevice,
                requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
        }));
        vkCheck(target.image.bindMemory(*target.memory, 0));

        target.cubeView = vkCheck(device.createImageView({
            .image = *target.image,
            .viewType = vk::ImageViewType::eCube,
            .format = depthFormat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = 6,
            },
        }));
        for (uint32_t face = 0; face < 6; ++face)
        {
            target.faceViews[face] = vkCheck(device.createImageView({
                .image = *target.image,
                .viewType = vk::ImageViewType::e2D,
                .format = depthFormat,
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eDepth,
                    .levelCount = 1,
                    .baseArrayLayer = face,
                    .layerCount = 1,
                },
            }));
        }
    }

    sampler = vkCheck(device.createSampler({
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    }));

    pipeline = pipelines.getOrCreate({
        .shader = shader,
        .layout = PipelineLayoutPreset::SceneOnly,
        .state = PipelineState::preset(RenderMode::Shadow),
        .depthFormat = depthFormat,
    });
    pipelineLayout = pipelines.layout(PipelineLayoutPreset::SceneOnly);

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const vk::DescriptorImageInfo imageInfo{
            .imageView = *targets[frameIndex].cubeView,
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo samplerInfo{.sampler = *sampler};
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet = targetFrames[frameIndex].sceneSetHandle(),
                .dstBinding = RenderInterface::shadowImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &imageInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet = targetFrames[frameIndex].sceneSetHandle(),
                .dstBinding = RenderInterface::shadowSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &samplerInfo,
            },
        };
        device.updateDescriptorSets(writes, {});
    }
}

void ShadowPass::reset() noexcept
{
    pipeline = nullptr;
    pipelineLayout = nullptr;
    sampler = nullptr;
    for (Target& target : targets)
    {
        for (auto& faceView : target.faceViews)
        {
            faceView = nullptr;
        }
        target.cubeView = nullptr;
        target.image = nullptr;
        target.memory = nullptr;
    }
    depthFormat = vk::Format::eUndefined;
    frames = nullptr;
}

void ShadowPass::transition(vk::raii::CommandBuffer& commandBuffer, vk::Image image,
    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
    vk::PipelineStageFlags2 srcStages, vk::AccessFlags2 srcAccess,
    vk::PipelineStageFlags2 dstStages, vk::AccessFlags2 dstAccess) const
{
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStages,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStages,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .levelCount = 1,
            .layerCount = 6,
        },
    };
    commandBuffer.pipelineBarrier2({
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

void ShadowPass::record(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex,
    const Scene::Desc::Object* lightObject,
    const std::vector<SceneRenderItem>& renderItems) const
{
    const Target& target = targets.at(frameIndex);
    const ecs::Light* light = nullptr;
    const ecs::Transform* transform = nullptr;
    if (lightObject != nullptr)
    {
        light = lightObject->components.at("Light").try_cast<ecs::Light>();
        transform = lightObject->components.at("Transform").try_cast<ecs::Transform>();
        CHECK(light != nullptr && transform != nullptr,
            "shadow light requires Light and Transform components");
    }

    const bool castShadow = light != nullptr && light->enabled && light->castShadow &&
        light->type == ecs::Light::Type::Point;
    if (!castShadow)
    {
        transition(commandBuffer, *target.image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, {}, {},
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        const vk::ImageSubresourceRange range{
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .levelCount = 1,
            .layerCount = 6,
        };
        commandBuffer.clearDepthStencilImage(*target.image,
            vk::ImageLayout::eTransferDstOptimal, {1.0f, 0}, range);
        transition(commandBuffer, *target.image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eDepthReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderSampledRead);
        return;
    }

    transition(commandBuffer, *target.image, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthAttachmentOptimal, {}, {},
        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite);

    CHECK(std::isfinite(light->range) && light->range > 0.0f,
        "shadow-casting point light requires a positive finite range");
    const float nearPlane = std::min(0.1f, light->range * 0.1f);
    const float farPlane = light->range;
    // Cubemap face rows follow EnvironmentMapUtils::faceDirection; the face up
    // vectors already account for Vulkan's downward-growing image coordinates.
    const glm::mat4 projection = glm::perspective(std::numbers::pi_v<float> * 0.5f,
        1.0f, nearPlane, farPlane);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sceneSets = {frames->at(frameIndex).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipelineLayout, RenderInterface::sceneSet, sceneSets, {});
    commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f,
        static_cast<float>(mapSize), static_cast<float>(mapSize), 0.0f, 1.0f));
    commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {mapSize, mapSize}));

    for (uint32_t face = 0; face < 6; ++face)
    {
        const vk::RenderingAttachmentInfo depthAttachment{
            .imageView = *target.faceViews[face],
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

        const glm::mat4 view = glm::lookAt(transform->position,
            transform->position + faceDirections[face], faceUp[face]);
        const glm::mat4 viewProjection = projection * view;
        for (const SceneRenderItem& item : renderItems)
        {
            const auto* objectTransform = item.object->components.at("Transform")
                .try_cast<ecs::Transform>();
            CHECK(objectTransform != nullptr, "shadow caster is missing Transform");

            const ShadowPushConstants push{
                .model = objectTransform->matrix(),
                .viewProjection = viewProjection,
            };
            commandBuffer.pushConstants<ShadowPushConstants>(pipelineLayout,
                vk::ShaderStageFlagBits::eVertex, 0, push);
            item.mesh->bind(commandBuffer);
            for (const Submesh& submesh : item.mesh->submeshes())
            {
                commandBuffer.drawIndexed(submesh.indexCount, 1,
                    submesh.firstIndex, 0, 0);
            }
        }
        commandBuffer.endRendering();
    }

    transition(commandBuffer, *target.image, vk::ImageLayout::eDepthAttachmentOptimal,
        vk::ImageLayout::eDepthReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
}
