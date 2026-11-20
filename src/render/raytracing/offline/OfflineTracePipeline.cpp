#include "render/raytracing/offline/OfflineTracePipeline.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/GpuShader.h"

#include <array>
#include <chrono>
#include <cstring>
#include <vector>

namespace
{
constexpr uint32_t kMissShaderCount = 2;
constexpr uint32_t kHitShaderCount = 1;

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

OfflineTracePipeline createTracePipeline(
    const VulkanContext& vulkan, vk::PipelineLayout layout, const std::string& shaderPath
)
{
    OfflineTracePipeline result;
    auto&           pipeline     = result.trace;
    auto&           bindingTable = result.bindingTable;
    auto&           raygenRegion = result.raygen;
    auto&           missRegion   = result.miss;
    auto&           hitRegion    = result.hit;

    const auto& device = vulkan.deviceHandle();
    const auto  properties =
        vulkan.physicalDeviceHandle()
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    CHECK(kRayPipelineRecursionDepth <= limits.maxRayRecursionDepth);

    GpuShader shader(device, shaderPath);

    const std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eRaygenKHR,
            .module = shader.handle(),
            .pName  = "raygenMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eMissKHR,
            .module = shader.handle(),
            .pName  = "missMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eMissKHR,
            .module = shader.handle(),
            .pName  = "visibilityMissMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eClosestHitKHR,
            .module = shader.handle(),
            .pName  = "closestHitMain"
        },
    };
    const std::array groups = {
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = 0,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = 1,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = 2,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .generalShader      = VK_SHADER_UNUSED_KHR,
            .closestHitShader   = 3,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
    };
    const vk::RayTracingPipelineCreateInfoKHR info{
        .stageCount                   = static_cast<uint32_t>(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = static_cast<uint32_t>(groups.size()),
        .pGroups                      = groups.data(),
        .maxPipelineRayRecursionDepth = kRayPipelineRecursionDepth,
        .layout                       = layout,
    };

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    vkCheck(
        static_cast<vk::Result>(device.getDispatcher()->vkCreateRayTracingPipelinesKHR(
            static_cast<VkDevice>(*device),
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            1,
            reinterpret_cast<const VkRayTracingPipelineCreateInfoKHR*>(&info),
            nullptr,
            &rawPipeline
        ))
    );
    pipeline = vk::raii::Pipeline(device, rawPipeline);

    const vk::DeviceSize stride     = alignUp(limits.shaderGroupHandleSize, limits.shaderGroupHandleAlignment);
    const vk::DeviceSize missOffset = alignUp(stride, limits.shaderGroupBaseAlignment);
    const vk::DeviceSize hitOffset  = alignUp(missOffset + stride * kMissShaderCount, limits.shaderGroupBaseAlignment);
    CHECK(stride <= limits.maxShaderGroupStride, "shader binding table stride exceeds the device limit");

    bindingTable = Buffer(
        vulkan.physicalDeviceHandle(),
        device,
        hitOffset + stride * kHitShaderCount + limits.shaderGroupBaseAlignment,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );

    const vk::DeviceAddress address     = bindingTable.address(device);
    const vk::DeviceAddress base        = alignUp(address, limits.shaderGroupBaseAlignment);
    const uint32_t          groupCount = static_cast<uint32_t>(groups.size());
    std::vector<uint8_t>    handles(limits.shaderGroupHandleSize * groupCount);
    vkCheck(
        static_cast<vk::Result>(device.getDispatcher()->vkGetRayTracingShaderGroupHandlesKHR(
            static_cast<VkDevice>(*device),
            static_cast<VkPipeline>(*pipeline),
            0,
            groupCount,
            handles.size(),
            handles.data()
        ))
    );

    std::vector<uint8_t> records(bindingTable.size());
    // Group order: raygen, path miss, visibility miss, surface hit.
    const std::array<vk::DeviceSize, 4> recordOffsets{0, missOffset, missOffset + stride, hitOffset};
    for (uint32_t group = 0; group < groupCount; ++group)
        std::memcpy(
            records.data() + base - address + recordOffsets[group],
            handles.data() + limits.shaderGroupHandleSize * group,
            limits.shaderGroupHandleSize
        );

    bindingTable.upload(records.data(), records.size());
    raygenRegion = vk::StridedDeviceAddressRegionKHR{.deviceAddress = base, .stride = stride, .size = stride};
    missRegion   = vk::StridedDeviceAddressRegionKHR{
          .deviceAddress = base + missOffset,
          .stride        = stride,
          .size          = stride * kMissShaderCount,
    };
    hitRegion = vk::StridedDeviceAddressRegionKHR{
        .deviceAddress = base + hitOffset,
        .stride        = stride,
        .size          = stride * kHitShaderCount
    };
    return result;
}

}  // namespace

const OfflineTracePipeline* OfflineTracePipelineCache::get(
    const VulkanContext& vulkan, vk::PipelineLayout layout, std::string_view shader, bool background
)
{
    auto [entry, inserted] = builds.try_emplace(std::pair{layout, std::string{shader}});
    auto& build = entry->second;
    if (inserted)
    {
        if (background)
        {
            build.future = std::async(std::launch::async, [&vulkan, layout, shaderPath = entry->first.second]
            {
                return createTracePipeline(vulkan, layout, shaderPath);
            });
        }
        else
        {
            build.result.emplace(createTracePipeline(vulkan, layout, entry->first.second));
        }
    }

    if (build.future.valid() && build.future.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        build.result.emplace(build.future.get());
    return build.result ? &*build.result : nullptr;
}

void OfflineTracePipelineCache::clear() noexcept
{
    // Join background builds before releasing the device and its cached layouts.
    builds.clear();
}
