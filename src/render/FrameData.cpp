#include "render/FrameData.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/Device.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/feature/editor/EditorShaderData.h"

#include <algorithm>
#include <array>
#include <cmath>


void FrameData::init(const VulkanContext& targetVulkan, Device& device)
{
    vulkan                = &targetVulkan;
    const auto hostMemory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    sceneBuffer           = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        sizeof(SceneUniforms),
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
    sceneSet    = device.allocateDescriptorSet(device.sceneDescriptorSetLayout);
    lightingSet = device.allocateDescriptorSet(device.lightingDescriptorSetLayout);
    viewSet     = device.allocateDescriptorSet(device.editorViewDescriptorSetLayout);
    writeSceneDescriptorSet();
    writeEditorViewDescriptorSet();
    writeLightingBufferDescriptors();
}

void FrameData::writeSceneDescriptorSet()
{
    const vk::DescriptorBufferInfo bufferInfo{.buffer = sceneBuffer.handle(), .range = sceneBuffer.size()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *sceneSet,
          .dstBinding      = RenderInterface::kSceneUniformBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo     = &bufferInfo,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeEditorViewDescriptorSet()
{
    const vk::DescriptorBufferInfo bufferInfo{.buffer = sceneBuffer.handle(), .range = sceneBuffer.size()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *viewSet,
          .dstBinding      = EditorInterface::kViewUniformBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo     = &bufferInfo,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeLightingBufferDescriptors()
{
    const std::array infos = {
        vk::DescriptorBufferInfo{.buffer = lightUniformBuffer.handle(), .range = lightUniformBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightBuffer.handle(), .range = lightBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = pointShadowBuffer.handle(), .range = pointShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = spotShadowBuffer.handle(), .range = spotShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = rectAreaShadowBuffer.handle(), .range = rectAreaShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = directionalShadowBuffer.handle(), .range = directionalShadowBuffer.size()},
    };
    const std::array writes = {
        vk::WriteDescriptorSet{
            .dstSet          = *lightingSet,
            .dstBinding      = RenderInterface::kLightUniformBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &infos[0]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *lightingSet,
            .dstBinding      = RenderInterface::kLightBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[1]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *lightingSet,
            .dstBinding      = RenderInterface::kPointShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[2]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *lightingSet,
            .dstBinding      = RenderInterface::kSpotShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[3]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *lightingSet,
            .dstBinding      = RenderInterface::kRectAreaShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[4]
        },
        vk::WriteDescriptorSet{
            .dstSet          = *lightingSet,
            .dstBinding      = RenderInterface::kDirectionalShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[5]
        },
    };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void FrameData::reset() noexcept
{
    sceneSet    = nullptr;
    lightingSet = nullptr;
    viewSet     = nullptr;
    sceneBuffer.reset();
    lightUniformBuffer.reset();
    lightBuffer.reset();
    pointShadowBuffer.reset();
    spotShadowBuffer.reset();
    rectAreaShadowBuffer.reset();
    directionalShadowBuffer.reset();

    sceneData     = {};
    lightMetadata = {};
    shadowData    = {};
    vulkan        = nullptr;
}

void FrameData::updateSceneUniforms(const SceneUniforms& view)
{
    DCHECK(vulkan);
    sceneData = view;
    sceneBuffer.upload(&sceneData, sizeof(sceneData));
}

void FrameData::updateLighting(const LightUniforms& lighting, std::span<const LightData> lights)
{
    DCHECK(vulkan && lighting.lightRanges.spotEnd == lights.size());
    lightMetadata = lighting;
    lightUniformBuffer.upload(&lightMetadata, sizeof(lightMetadata));

    shadowData = {};
    shadowData.lights.assign(lights.begin(), lights.end());
    for (auto& light : shadowData.lights)
    {
        light.shadowEnabled = 0;
        light.shadowidx     = ~0u;
    }
    uploadLights();
}

void FrameData::uploadLights()
{
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
    {
        const vk::DescriptorBufferInfo bufferInfo{.buffer = lightBuffer.handle(), .range = lightBuffer.size()};
        const vk::WriteDescriptorSet   write{
              .dstSet          = *lightingSet,
              .dstBinding      = RenderInterface::kLightBufferBinding,
              .descriptorCount = 1,
              .descriptorType  = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo     = &bufferInfo,
        };
        vulkan->deviceHandle().updateDescriptorSets(write, {});
    }

    if (!shadowData.lights.empty())
        lightBuffer.upload(shadowData.lights.data(), shadowData.lights.size() * sizeof(LightData));
}

void FrameData::updateShadows(ShadowFrameData prepared)
{
    DCHECK(vulkan && lightMetadata.lightRanges.spotEnd == prepared.lights.size());
    shadowData = std::move(prepared);
    uploadLights();

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
