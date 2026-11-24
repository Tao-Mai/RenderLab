#pragma once
#include <future>
#include <map>
#include <optional>
#include <slang-rhi.h>
#include "render/resources/ShaderManager.h"
class OfflineTracePipelineCache
{
public:
    struct Result
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable>        shaderTable;
    };
    [[nodiscard]] const Result* get(rhi::IDevice& device, ShaderManager& shaders, ShaderHandle shader, bool background);
    void                        clear() noexcept;

private:
    struct Build
    {
        std::optional<Result> result;
        std::future<Result>   future;
    };
    std::map<uint32_t, Build> builds;
};
