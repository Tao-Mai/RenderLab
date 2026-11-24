#include "render/raytracing/PathPipelineCache.h"
#include <array>
#include <chrono>
#include <slang-rhi.h>
#include "render/RenderConfig.h"
#include "render/device/RhiCheck.h"
namespace
{
PathTracingProgram buildPipeline(rhi::IDevice& device, rhi::IShaderProgram* program)
{
    constexpr std::array kRaygen{"raygenMain"};
    PathTracingProgram   result;
    result.create(device, program, kRaygen);
    return result;
}
}  // namespace

const PathTracingProgram*
PathPipelineCache::get(rhi::IDevice& device, ShaderManager& shaders, ShaderHandle shader, bool background)
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

void PathPipelineCache::clear() noexcept { builds.clear(); }
