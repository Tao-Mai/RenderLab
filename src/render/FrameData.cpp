#include "render/FrameData.h"
#include "render/feature/SceneInterface.h"
#include "render/feature/editor/EditorInterface.h"

#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <array>
#include <cmath>


void FrameData::init(const VulkanContext& targetVulkan, DescriptorManager& descriptors)
{
    vulkan                = &targetVulkan;
    const auto hostMemory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    viewBuffer            = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        sizeof(ViewUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        hostMemory
    );
    lightUniformBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        sizeof(LightUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        hostMemory
    );
    // Even a scene without lights needs a nonempty, valid storage descriptor.
    lightBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        sizeof(LightData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    pointShadowBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        kMaxPointLightShadow * sizeof(PointShadowData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    spotShadowBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        kMaxSpotLightShadow * sizeof(SpotShadowData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    rectAreaShadowBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        kMaxRectAreaLightShadow * sizeof(RectAreaShadowData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    directionalShadowBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        kMaxDirectionalLightShadow * sizeof(DirectionalShadowData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    sceneSet = descriptors.allocate(renderContext().sceneInterface->sceneLayout);
    viewSet  = descriptors.allocate(EditorInterface::viewLayout(descriptors));
    bindBuffers();
}

void FrameData::bindBuffers()
{
    const std::array infos = {
        vk::DescriptorBufferInfo{.buffer = viewBuffer.handle(), .range = viewBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightUniformBuffer.handle(), .range = lightUniformBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightBuffer.handle(), .range = lightBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = pointShadowBuffer.handle(), .range = pointShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = spotShadowBuffer.handle(), .range = spotShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = rectAreaShadowBuffer.handle(), .range = rectAreaShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = directionalShadowBuffer.handle(), .range = directionalShadowBuffer.size()},
    };
    const std::array writes = {
        vk::WriteDescriptorSet{
            .dstSet          = *viewSet,
            .dstBinding      = RenderInterface::kViewUniformBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &infos[0]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kViewUniformBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &infos[0]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kLightUniformBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &infos[1]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kLightBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[2]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kPointShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[3]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kSpotShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[4]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kRectAreaShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[5]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *sceneSet,
            .dstBinding      = RenderInterface::kDirectionalShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[6]
        },
    };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void FrameData::reset() noexcept
{
    sceneSet = nullptr;
    viewSet  = nullptr;
    viewBuffer.reset();
    lightUniformBuffer.reset();
    lightBuffer.reset();
    pointShadowBuffer.reset();
    spotShadowBuffer.reset();
    rectAreaShadowBuffer.reset();
    directionalShadowBuffer.reset();

    viewData      = {};
    lightMetadata = {};
    shadowData    = {};
    vulkan        = nullptr;
}

void FrameData::updateView(const ViewUniforms& view)
{
    DCHECK(vulkan);
    viewData = view;
    viewBuffer.upload(&viewData, sizeof(viewData));
}

void FrameData::update(const ViewUniforms& view, const LightUniforms& lighting, ShadowFrameData prepared)
{
    DCHECK(vulkan && lighting.lightRanges.spotEnd == prepared.lights.size());
    DCHECK(lighting.lightRanges == prepared.lightRanges);
    shadowData = std::move(prepared);

    const auto ensureStorage = [&](Buffer& buffer, vk::DeviceSize bytes)
    {
        CHECK(bytes <= vulkan->properties().limits.maxStorageBufferRange, "frame SSBO exceeds maxStorageBufferRange");
        if (bytes <= buffer.size())
            return false;

        // The current frame's fence is complete; other frames keep their buffers.
        buffer.reset();
        buffer = Buffer(
            vulkan->physicalDeviceHandle(),
            vulkan->deviceHandle(),
            bytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        return true;
    };
    if (ensureStorage(lightBuffer, std::max<size_t>(shadowData.lights.size(), 1) * sizeof(LightData)))
        bindBuffers();  // 有多少上传多少，而不是固定按最大数量

    updateView(view);
    lightMetadata = lighting;
    lightUniformBuffer.upload(&lightMetadata, sizeof(lightMetadata));
    if (!shadowData.lights.empty())
        lightBuffer.upload(shadowData.lights.data(), shadowData.lights.size() * sizeof(LightData));
    if (!shadowData.pointShadows.empty())
        pointShadowBuffer.upload(
            shadowData.pointShadows.data(),
            shadowData.pointShadows.size() * sizeof(PointShadowData)
        );
    if (!shadowData.spotShadows.empty())
        spotShadowBuffer.upload(shadowData.spotShadows.data(), shadowData.spotShadows.size() * sizeof(SpotShadowData));
    if (!shadowData.rectAreaShadows.empty())
        rectAreaShadowBuffer.upload(
            shadowData.rectAreaShadows.data(),
            shadowData.rectAreaShadows.size() * sizeof(RectAreaShadowData)
        );
    if (!shadowData.directionalShadows.empty())
        directionalShadowBuffer.upload(
            shadowData.directionalShadows.data(),
            shadowData.directionalShadows.size() * sizeof(DirectionalShadowData)
        );
    // Host-coherent writes become available to device reads at queue submission.
}
