#include "render/raytracing/offline/OfflineTracePipelineCache.h"
#include <array>
#include <chrono>
#include <slang-rhi.h>
#include "render/RenderConfig.h"
#include "render/device/RhiCheck.h"
namespace
{
OfflineTracePipelineCache::Result buildPipeline(rhi::IDevice& device, rhi::IShaderProgram* program)
{
    constexpr std::array        kRaygen{"raygenMain"};
    constexpr std::array        kMiss{"missMain", "visibilityMissMain"};
    constexpr std::array        kGroupNames{"hitGroup0"};
    const rhi::HitGroupDesc     group{"hitGroup0", "closestHitMain"};
    rhi::RayTracingPipelineDesc desc;
    desc.program           = program;
    desc.hitGroups         = &group;
    desc.hitGroupCount     = 1;
    desc.maxRecursion      = kRayPipelineRecursionDepth;
    desc.maxRayPayloadSize = kPathTracingPayloadBytes;
    OfflineTracePipelineCache::Result result;
    rhiCheck(device.createRayTracingPipeline(desc, result.pipeline.writeRef()));
    rhi::ShaderTableDesc table;
    table.program                     = program;
    table.rayGenShaderEntryPointNames = kRaygen.data();
    table.rayGenShaderCount           = uint32_t(kRaygen.size());
    table.missShaderEntryPointNames   = kMiss.data();
    table.missShaderCount             = uint32_t(kMiss.size());
    table.hitGroupNames               = kGroupNames.data();
    table.hitGroupCount               = uint32_t(kGroupNames.size());
    rhiCheck(device.createShaderTable(table, result.shaderTable.writeRef()));
    return result;
}
}  // namespace

const OfflineTracePipelineCache::Result*
OfflineTracePipelineCache::get(rhi::IDevice& device, ShaderManager& shaders, ShaderHandle shader, bool background)
{
    auto [entry, inserted] = builds.try_emplace(shader.index);
    auto& build            = entry->second;
    if (inserted)
    {
        rhi::ComPtr<rhi::IShaderProgram> program(shaders.program(shader));
        if (background)
            build.future =
                std::async(std::launch::async, [&device, program] { return buildPipeline(device, program); });
        else
            build.result.emplace(buildPipeline(device, program));
    }
    if (build.future.valid() && build.future.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        build.result.emplace(build.future.get());
    return build.result ? &*build.result : nullptr;
}

void OfflineTracePipelineCache::clear() noexcept { builds.clear(); }
