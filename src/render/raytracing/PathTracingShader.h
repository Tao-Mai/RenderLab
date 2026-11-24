#pragma once

#include "core/Logger.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/device/RhiCheck.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <future>
#include <slang-rhi.h>
#include <span>

[[nodiscard]] inline bool checkRayTracingSupport(rhi::IDevice& device)
{
    if (device.hasFeature(rhi::Feature::RayTracing))
        return true;

    LOG_ERROR("requires ray tracing");
    return false;
}

enum class PathEnvironment : uint32_t
{
    None,
    EqualArea,
    Cubemap,
    EqualAreaAndCubemap,
    Count,
};

inline PathEnvironment pathEnvironment(const GpuScene& scene)
{
    if (scene.environmentMap())
        return scene.hasEnvironmentLight() ? PathEnvironment::EqualAreaAndCubemap : PathEnvironment::Cubemap;
    return scene.hasEnvironmentLight() ? PathEnvironment::EqualArea : PathEnvironment::None;
}

inline ShaderDefines pathTracingDefines(PathEnvironment environment)
{
    return {
        {"SCENE_RAY_TRACING", "1"},
        {"ENABLE_CLEARCOAT", "0"},
        {"ENABLE_SHEEN", "0"},
        {"HAS_ENV", environment != PathEnvironment::None ? "1" : "0"},
        {"HAS_ENV_EQUAL_AREA",
         environment == PathEnvironment::EqualArea || environment == PathEnvironment::EqualAreaAndCubemap ? "1" : "0"},
        {"HAS_ENV_CUBEMAP",
         environment == PathEnvironment::Cubemap || environment == PathEnvironment::EqualAreaAndCubemap ? "1" : "0"},
    };
}

[[nodiscard]] inline PathTracingParameters
makePathTracingParameters(const GpuScene& scene, uint32_t maxBounces, uint32_t sampleIndex)
{
    const auto& viewport = scene.viewport();
    return {
        .viewport    = {viewport.x, viewport.y, viewport.width, viewport.height},
        .maxBounces  = maxBounces,
        .sampleIndex = sampleIndex,
    };
}

[[nodiscard]] rhi::RayTracingPipelineDesc pathTracingPipelineDesc(rhi::IShaderProgram* program);

[[nodiscard]] std::future<rhi::ComPtr<rhi::IRayTracingPipeline>>
buildPathTracingPipelineAsync(rhi::IDevice& device, rhi::IShaderProgram* program);

void waitForPathTracingPipelineBuilds() noexcept;

[[nodiscard]] rhi::ComPtr<rhi::IShaderTable> createPathTracingShaderTable(
    rhi::IDevice& device, rhi::IShaderProgram* program, std::span<const char* const> raygenEntries
);
