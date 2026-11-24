#pragma once
#include <future>
#include <map>
#include <optional>
#include <slang-rhi.h>
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/ShaderManager.h"
class PathPipelineCache
{
public:
    [[nodiscard]] const PathTracingProgram*
         get(rhi::IDevice& device, ShaderManager& shaders, ShaderHandle shader, bool background);
    void clear() noexcept;

private:
    struct Build
    {
        std::optional<PathTracingProgram> result;
        std::future<PathTracingProgram>   future;
    };
    std::map<uint32_t, Build> builds;
};
