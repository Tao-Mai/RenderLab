#pragma once

#include "render/PreparedScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <slang-rhi.h>
#include <span>

inline constexpr std::array kPathTracingMissEntries{"missMain", "visibilityMissMain"};
inline constexpr std::array kPathTracingHitEntries{"closestHitMain", "lightClosestHitMain"};

enum class PathEnvironment : uint32_t
{
    None,
    EqualArea,
    Cubemap,
    EqualAreaAndCubemap,
    Count,
};

inline PathEnvironment pathEnvironment(const PreparedScene& scene)
{
    if (scene.environmentMap)
        return scene.shading->hasEnvironmentLight ? PathEnvironment::EqualAreaAndCubemap : PathEnvironment::Cubemap;
    return scene.shading->hasEnvironmentLight ? PathEnvironment::EqualArea : PathEnvironment::None;
}

inline ShaderDefines pathEnvironmentDefines(PathEnvironment environment)
{
    return {
        {"HAS_ENV", environment != PathEnvironment::None ? "1" : "0"},
        {"HAS_ENV_EQUAL_AREA",
         environment == PathEnvironment::EqualArea || environment == PathEnvironment::EqualAreaAndCubemap ? "1" : "0"},
        {"HAS_ENV_CUBEMAP",
         environment == PathEnvironment::Cubemap || environment == PathEnvironment::EqualAreaAndCubemap ? "1" : "0"},
    };
}

[[nodiscard]] inline PathTracingParameters
makePathTracingParameters(const RenderContext& frame, uint32_t maxBounces, uint32_t sampleIndex)
{
    const auto& viewport = frame.view->viewport;
    return {
        .viewport    = {viewport.x, viewport.y, viewport.width, viewport.height},
        .maxBounces  = maxBounces,
        .sampleIndex = sampleIndex,
    };
}

struct PathTracingProgram
{
    rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
    rhi::ComPtr<rhi::IShaderTable>        shaderTable;

    void create(rhi::IDevice& device, rhi::IShaderProgram* program, std::span<const char* const> raygenEntries)
    {
        constexpr std::array kHitGroupNames{"hitGroup0", "hitGroup1"};
        const std::array     groups{
            rhi::HitGroupDesc{kHitGroupNames[0], kPathTracingHitEntries[0]},
            rhi::HitGroupDesc{kHitGroupNames[1], kPathTracingHitEntries[1]}
        };
        rhi::RayTracingPipelineDesc desc;
        desc.program           = program;
        desc.hitGroups         = groups.data();
        desc.hitGroupCount     = uint32_t(groups.size());
        desc.maxRecursion      = kRayPipelineRecursionDepth;
        desc.maxRayPayloadSize = kPathTracingPayloadBytes;
        rhiCheck(device.createRayTracingPipeline(desc, pipeline.writeRef()));

        rhi::ShaderTableDesc tableDesc;
        tableDesc.program                     = program;
        tableDesc.rayGenShaderEntryPointNames = raygenEntries.data();
        tableDesc.rayGenShaderCount           = uint32_t(raygenEntries.size());
        tableDesc.missShaderEntryPointNames   = kPathTracingMissEntries.data();
        tableDesc.missShaderCount             = uint32_t(kPathTracingMissEntries.size());
        tableDesc.hitGroupNames               = kHitGroupNames.data();
        tableDesc.hitGroupCount               = uint32_t(kHitGroupNames.size());
        rhiCheck(device.createShaderTable(tableDesc, shaderTable.writeRef()));
    }
};
