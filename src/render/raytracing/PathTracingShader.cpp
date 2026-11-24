#include "render/raytracing/PathTracingShader.h"

#include <array>
#include <chrono>
#include <vector>

namespace
{
constexpr uint32_t kRayPipelineRecursionDepth = 1;
constexpr uint32_t kPathTracingPayloadBytes = 256;
constexpr std::array kMissEntries{"missMain", "visibilityMissMain"};
constexpr std::array kHitGroups{
    rhi::HitGroupDesc{"hitGroup0", "closestHitMain"},
};
constexpr std::array kHitGroupNames{kHitGroups[0].hitGroupName};

std::vector<std::future<void>>& activePipelineBuilds()
{
    static std::vector<std::future<void>> builds;
    return builds;
}
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

std::future<rhi::ComPtr<rhi::IRayTracingPipeline>>
buildPathTracingPipelineAsync(rhi::IDevice& device, rhi::IShaderProgram* program)
{
    auto& builds = activePipelineBuilds();
    std::erase_if(
        builds,
        [](const auto& build) { return build.wait_for(std::chrono::seconds{0}) == std::future_status::ready; }
    );

    // The pass can discard the result on graph changes. Keep its Slang session alive until the build ends.
    std::promise<rhi::ComPtr<rhi::IRayTracingPipeline>> promise;
    auto result = promise.get_future();
    builds.push_back(std::async(
        std::launch::async,
        [device = rhi::ComPtr{&device},
         session = rhi::ComPtr{program->getSlangComponentType()->getSession()},
         program = rhi::ComPtr{program},
         promise = std::move(promise)]() mutable
        {
            auto desc = pathTracingPipelineDesc(program);
            desc.compilationPolicy = rhi::PipelineCompilationPolicy::Immediate;

            rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
            rhiCheck(device->createRayTracingPipeline(desc, pipeline.writeRef()));

            auto completion = std::move(promise);
            completion.set_value(std::move(pipeline));
        }
    ));
    return result;
}

void waitForPathTracingPipelineBuilds() noexcept
{
    activePipelineBuilds().clear();
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
