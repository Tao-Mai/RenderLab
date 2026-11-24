#pragma once

#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/RhiCheck.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <cstddef>
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

inline constexpr std::size_t kPathEnvironmentVariantCount = 2;

inline ShaderDefines pathTracingDefines(bool hasEnvironment)
{
    return {
        {"SCENE_RAY_TRACING", "1"},
        {"ENABLE_CLEARCOAT", "0"},
        {"ENABLE_SHEEN", "0"},
        {"HAS_ENV", hasEnvironment ? "1" : "0"},
    };
}

[[nodiscard]] rhi::RayTracingPipelineDesc pathTracingPipelineDesc(rhi::IShaderProgram* program);

[[nodiscard]] std::future<rhi::ComPtr<rhi::IRayTracingPipeline>>
buildPathTracingPipelineAsync(rhi::IDevice& device, rhi::IShaderProgram* program);

void waitForPathTracingPipelineBuilds() noexcept;

[[nodiscard]] rhi::ComPtr<rhi::IShaderTable> createPathTracingShaderTable(
    rhi::IDevice& device, rhi::IShaderProgram* program, std::span<const char* const> raygenEntries
);
