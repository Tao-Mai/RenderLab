#include "render/FrameData.h"

#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/RenderConfig.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

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

// Face up vectors account for Vulkan's downward-growing image coordinates.
constexpr std::array faceUp = {
    glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
    glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f},
};
}

void FrameData::init(const VulkanContext& targetVulkan, DescriptorManager& descriptors)
{
    vulkan                = &targetVulkan;
    const auto hostMemory = vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent;
    viewBuffer = Buffer(vulkan->physicalDeviceHandle(),
                        vulkan->deviceHandle(),
                        sizeof(ViewUniforms),
                        vk::BufferUsageFlagBits::eUniformBuffer,
                        hostMemory);
    lightUniformBuffer = Buffer(vulkan->physicalDeviceHandle(),
                                vulkan->deviceHandle(),
                                sizeof(LightUniforms),
                                vk::BufferUsageFlagBits::eUniformBuffer,
                                hostMemory);
    // Even a scene without lights needs a nonempty, valid storage descriptor.
    lightBuffer = Buffer(vulkan->physicalDeviceHandle(),
                         vulkan->deviceHandle(),
                         sizeof(LightData),
                         vk::BufferUsageFlagBits::eStorageBuffer,
                         hostMemory);
    pointShadowBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        kMaxPointLightShadow * sizeof(PointShadowData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    pointShadows.reserve(kMaxPointLightShadow);
    rectAreaShadowBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        kMaxRectAreaLightShadow * sizeof(RectAreaShadowData),
        vk::BufferUsageFlagBits::eStorageBuffer,
        hostMemory
    );
    rectAreaShadows.reserve(kMaxRectAreaLightShadow);
    sceneSet = descriptors.allocate(DescriptorLayoutPreset::Scene);
    bindBuffers();
}

void FrameData::bindBuffers()
{
    const std::array infos = {
        vk::DescriptorBufferInfo{.buffer = viewBuffer.handle(), .range = viewBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightUniformBuffer.handle(), .range = lightUniformBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = lightBuffer.handle(), .range = lightBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = pointShadowBuffer.handle(), .range = pointShadowBuffer.size()},
        vk::DescriptorBufferInfo{.buffer = rectAreaShadowBuffer.handle(), .range = rectAreaShadowBuffer.size()},
    };
    const std::array writes = {
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
            .dstBinding      = RenderInterface::kRectAreaShadowBufferBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[4]
        },
    };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void FrameData::reset() noexcept
{
    sceneSet = nullptr;
    viewBuffer.reset();
    lightUniformBuffer.reset();
    lightBuffer.reset();
    pointShadowBuffer.reset();
    rectAreaShadowBuffer.reset();

    viewData      = {};
    lightMetadata = {};
    lights.clear();
    pointShadows.clear();
    rectAreaShadows.clear();
    vulkan = nullptr;
}

void FrameData::update(const ViewUniforms&        view, const LightUniforms& lighting,
                       std::span<const LightData> frameLights, bool shadowsEnabled)
{
    DCHECK(vulkan && lighting.lightCount == frameLights.size(),
           "frame light count must match the uploaded SSBO elements");
    lights.assign(frameLights.begin(), frameLights.end());
    pointShadows.clear();
    rectAreaShadows.clear();
    for (LightData& light : lights)
    {
        light.shadowidx = ~0u;
        if (!shadowsEnabled)
            light.flags.z = 0;
        if (!light.enable() || !light.castShadow() ||
            (light.type() != LightComponent::Type::Point && light.type() != LightComponent::Type::RectArea))
            continue;

        const float range = light.range();
        CHECK(std::isfinite(range) && range > 0.0f);
        const glm::vec3 position  = light.position();

        if (light.type() == LightComponent::Type::Point)
        {
            light.shadowidx = static_cast<uint32_t>(pointShadows.size());
            CHECK(
                light.shadowidx < kMaxPointLightShadow,
                "enabled shadow-casting point lights exceed capacity {}",
                kMaxPointLightShadow
            );

            const float     nearPlane  = std::min(0.1f, range * 0.1f);
            const glm::mat4 projection = glm::perspective(std::numbers::pi_v<float> * 0.5f, 1.0f, nearPlane, range);
            PointShadowData& shadow     = pointShadows.emplace_back();
            for (size_t face = 0; face < shadow.vp.size(); ++face)
                shadow.vp[face] = projection * glm::lookAt(position, position + faceDirections[face], faceUp[face]);
        }
        else
        {
            light.shadowidx = static_cast<uint32_t>(rectAreaShadows.size());
            CHECK(
                light.shadowidx < kMaxRectAreaLightShadow,
                "enabled shadow-casting rect area lights exceed capacity {}",
                kMaxRectAreaLightShadow
            );
            const glm::vec2 areaSize{light.areaSizeCone};
            CHECK(std::isfinite(areaSize.x) && std::isfinite(areaSize.y) && areaSize.x > 0.0f && areaSize.y > 0.0f);

            const glm::vec3 direction = glm::normalize(glm::vec3{light.direction});
            const glm::vec3 up        = glm::normalize(glm::cross(light.areaRight(), direction));
            const float     nearPlane = std::min(0.5f, range * 0.1f);
            glm::mat4       projection =
                glm::perspective(glm::radians(100.0f), areaSize.x / areaSize.y, nearPlane, range);
            projection[1][1] *= -1.0f;
            rectAreaShadows.push_back({
                .vp = projection * glm::lookAt(position, position + direction, up),
                .nearPlane = nearPlane,
                .farPlane = range,
                .worldToUvScale = {0.5f * std::abs(projection[0][0]), 0.5f * std::abs(projection[1][1])},
            });
        }
    }

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
    if (ensureStorage(lightBuffer, std::max<size_t>(lights.size(), 1) * sizeof(LightData)))
        bindBuffers();  // 有多少上传多少，而不是固定按最大数量

    viewData      = view;
    lightMetadata = lighting;
    viewBuffer.upload(&viewData, sizeof(viewData));
    lightUniformBuffer.upload(&lightMetadata, sizeof(lightMetadata));
    if (!lights.empty())
        lightBuffer.upload(lights.data(), lights.size() * sizeof(LightData));
    if (!pointShadows.empty())
        pointShadowBuffer.upload(pointShadows.data(), pointShadows.size() * sizeof(PointShadowData));
    if (!rectAreaShadows.empty())
        rectAreaShadowBuffer.upload(rectAreaShadows.data(), rectAreaShadows.size() * sizeof(RectAreaShadowData));
    // Host-coherent writes become available to device reads at queue submission.
}

const ViewUniforms&              FrameData::viewUniforms() const { return viewData; }
const LightUniforms&             FrameData::lightUniforms() const { return lightMetadata; }
std::span<const LightData>       FrameData::lightData() const { return lights; }
std::span<const PointShadowData> FrameData::pointShadowData() const { return pointShadows; }

vk::DescriptorSet FrameData::sceneSetHandle() const { return *sceneSet; }
