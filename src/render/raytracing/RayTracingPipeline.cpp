#include "render/raytracing/RayTracingPipeline.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <cstddef>
#include <cstring>

namespace
{
vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}
}

RayTracingPipeline::RayTracingPipeline(const VulkanContext& vulkan, const Description& description) :
    pipelineLayout(description.layout)
{
    DCHECK(!description.raygen.empty());
    CHECK(vulkan.supportsRayTracing(), "selected GPU does not support ray tracing pipelines");
    const auto& device = vulkan.deviceHandle();
    const auto properties = vulkan.physicalDeviceHandle()
        .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    CHECK(description.maxRecursionDepth <= limits.maxRayRecursionDepth,
          "ray tracing requests recursion depth {}, GPU supports {}",
          description.maxRecursionDepth, limits.maxRayRecursionDepth);

    const auto entryCount = description.raygen.size() + description.miss.size() + description.closestHit.size();
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;
    stages.reserve(entryCount);
    groups.reserve(entryCount);
    const auto append = [&](std::span<const char* const> entries, vk::ShaderStageFlagBits stage)
    {
        const bool hit = stage == vk::ShaderStageFlagBits::eClosestHitKHR;
        for (const char* entry : entries)
        {
            const auto index = static_cast<uint32_t>(stages.size());
            stages.push_back({.stage = stage, .module = description.shader, .pName = entry});
            groups.push_back({
                .type = hit ? vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup :
                              vk::RayTracingShaderGroupTypeKHR::eGeneral,
                .generalShader = hit ? VK_SHADER_UNUSED_KHR : index,
                .closestHitShader = hit ? index : VK_SHADER_UNUSED_KHR,
                .anyHitShader = VK_SHADER_UNUSED_KHR,
                .intersectionShader = VK_SHADER_UNUSED_KHR,
            });
        }
    };
    append(description.raygen, vk::ShaderStageFlagBits::eRaygenKHR);
    append(description.miss, vk::ShaderStageFlagBits::eMissKHR);
    append(description.closestHit, vk::ShaderStageFlagBits::eClosestHitKHR);

    const vk::RayTracingPipelineCreateInfoKHR info{
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .groupCount = static_cast<uint32_t>(groups.size()),
        .pGroups = groups.data(),
        .maxPipelineRayRecursionDepth = description.maxRecursionDepth,
        .layout = pipelineLayout,
    };
    VkPipeline handle = VK_NULL_HANDLE;
    vkCheck(static_cast<vk::Result>(device.getDispatcher()->vkCreateRayTracingPipelinesKHR(
        static_cast<VkDevice>(*device), VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
        reinterpret_cast<const VkRayTracingPipelineCreateInfoKHR*>(&info), nullptr, &handle
    )));
    pipeline = vk::raii::Pipeline(device, handle);

    buildBindingTable(vulkan, limits, static_cast<uint32_t>(description.raygen.size()),
                      static_cast<uint32_t>(description.miss.size()), static_cast<uint32_t>(description.closestHit.size()));
}

void RayTracingPipeline::buildBindingTable(
    const VulkanContext& vulkan,
    const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& limits,
    uint32_t raygenCount, uint32_t missCount, uint32_t hitCount
)
{
    const auto& device = vulkan.deviceHandle();
    const uint32_t handleSize = limits.shaderGroupHandleSize;
    const auto stride = alignUp(handleSize, limits.shaderGroupHandleAlignment);
    const auto raygenSpacing = alignUp(stride, limits.shaderGroupBaseAlignment);
    const auto missOffset = raygenSpacing * raygenCount;
    const auto hitOffset = alignUp(missOffset + stride * missCount, limits.shaderGroupBaseAlignment);
    CHECK(stride <= limits.maxShaderGroupStride, "shader binding table stride exceeds the device limit");

    bindingTable = Falcor::Buffer(
        vulkan, hitOffset + stride * hitCount + limits.shaderGroupBaseAlignment,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        Falcor::MemoryType::Upload,
        Falcor::ResourceBindFlags::None
    );
    const auto address = bindingTable.getGpuAddress();
    const auto base = alignUp(address, limits.shaderGroupBaseAlignment);
    const uint32_t groupCount = raygenCount + missCount + hitCount;
    std::vector<std::byte> handles(vk::DeviceSize(handleSize) * groupCount);
    vkCheck(static_cast<vk::Result>(device.getDispatcher()->vkGetRayTracingShaderGroupHandlesKHR(
        static_cast<VkDevice>(*device), static_cast<VkPipeline>(*pipeline), 0,
        groupCount, handles.size(), handles.data()
    )));

    std::vector<std::byte> records(bindingTable.getSize());
    const auto copyRecords = [&](uint32_t firstGroup, uint32_t count, vk::DeviceSize offset, vk::DeviceSize spacing)
    {
        for (uint32_t index = 0; index < count; ++index)
            std::memcpy(records.data() + (base - address) + offset + spacing * index,
                        handles.data() + vk::DeviceSize(handleSize) * (firstGroup + index), handleSize);
    };
    copyRecords(0, raygenCount, 0, raygenSpacing);
    copyRecords(raygenCount, missCount, missOffset, stride);
    copyRecords(raygenCount + missCount, hitCount, hitOffset, stride);
    bindingTable.setBlob(records.data(), 0, records.size());

    raygenRegions.reserve(raygenCount);
    for (uint32_t index = 0; index < raygenCount; ++index)
        raygenRegions.push_back({.deviceAddress = base + raygenSpacing * index, .stride = stride, .size = stride});
    if (missCount != 0)
        missRegion = vk::StridedDeviceAddressRegionKHR{
            .deviceAddress = base + missOffset, .stride = stride, .size = stride * missCount,
        };
    if (hitCount != 0)
        hitRegion = vk::StridedDeviceAddressRegionKHR{
            .deviceAddress = base + hitOffset, .stride = stride, .size = stride * hitCount,
        };
}

void RayTracingPipeline::bind(vk::raii::CommandBuffer& commands) const
{
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
}

void RayTracingPipeline::dispatch(vk::raii::CommandBuffer& commands, uint32_t raygenIndex, vk::Extent3D extent) const
{
    DCHECK(raygenIndex < raygenRegions.size());
    commands.traceRaysKHR(raygenRegions[raygenIndex], missRegion, hitRegion, {}, extent.width, extent.height, extent.depth);
}
