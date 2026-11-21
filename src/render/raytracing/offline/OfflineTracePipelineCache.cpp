#include "render/raytracing/offline/OfflineTracePipelineCache.h"

#include "render/RenderConfig.h"
#include "render/device/VulkanContext.h"
#include "render/resources/GpuShader.h"

#include <array>
#include <chrono>

namespace
{
RayTracingPipeline buildPipeline(const VulkanContext& vulkan, vk::PipelineLayout layout, const std::string& shaderPath)
{
    constexpr std::array kRaygenEntries{"raygenMain"};
    constexpr std::array kMissEntries{"missMain", "visibilityMissMain"};
    constexpr std::array kHitEntries{"closestHitMain"};
    const GpuShader shader(vulkan.deviceHandle(), shaderPath);
    return RayTracingPipeline(vulkan, {
        .shader = shader.handle(),
        .layout = layout,
        .raygen = kRaygenEntries,
        .miss = kMissEntries,
        .closestHit = kHitEntries,
        .maxRecursionDepth = kRayPipelineRecursionDepth,
    });
}
}

const RayTracingPipeline* OfflineTracePipelineCache::get(
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
                return buildPipeline(vulkan, layout, shaderPath);
            });
        }
        else
            build.result.emplace(buildPipeline(vulkan, layout, entry->first.second));
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
