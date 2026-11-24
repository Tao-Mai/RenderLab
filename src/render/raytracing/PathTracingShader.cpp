#include "render/raytracing/PathTracingShader.h"

#include <array>

namespace
{
constexpr uint32_t kRayPipelineRecursionDepth = 1;
constexpr uint32_t kPathTracingPayloadBytes = 256;
constexpr std::array kMissEntries{"missMain", "visibilityMissMain"};
// Match the hit record indices used when building the scene TLAS.
constexpr std::array kHitGroups{
    rhi::HitGroupDesc{"hitGroup0", "closestHitMain"},
    rhi::HitGroupDesc{"hitGroup1", "lightClosestHitMain"},
};
constexpr std::array kHitGroupNames{kHitGroups[0].hitGroupName, kHitGroups[1].hitGroupName};
}

rhi::RayTracingPipelineDesc pathTracingPipelineDesc(rhi::IShaderProgram* program)
{
    return {
        .program           = program,
        .hitGroupCount     = uint32_t(kHitGroups.size()),
        .hitGroups         = kHitGroups.data(),
        .maxRecursion      = kRayPipelineRecursionDepth,
        .maxRayPayloadSize = kPathTracingPayloadBytes,
    };
}

rhi::ComPtr<rhi::IShaderTable> createPathTracingShaderTable(
    rhi::IDevice& device, rhi::IShaderProgram* program, std::span<const char* const> raygenEntries
)
{
    const rhi::ShaderTableDesc desc{
        .rayGenShaderCount           = uint32_t(raygenEntries.size()),
        .rayGenShaderEntryPointNames = raygenEntries.data(),
        .missShaderCount             = uint32_t(kMissEntries.size()),
        .missShaderEntryPointNames   = kMissEntries.data(),
        .hitGroupCount               = uint32_t(kHitGroupNames.size()),
        .hitGroupNames               = kHitGroupNames.data(),
        .program                     = program,
    };
    rhi::ComPtr<rhi::IShaderTable> shaderTable;
    rhiCheck(device.createShaderTable(desc, shaderTable.writeRef()));
    return shaderTable;
}
