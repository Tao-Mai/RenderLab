#pragma once

#include <slang-rhi.h>

#include <filesystem>
#include <future>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using ShaderDefines = std::map<std::string, std::string>;

class ShaderManager
{
public:
    void init(rhi::IDevice* device);
    void shutdown() noexcept;

    [[nodiscard]] rhi::IShaderProgram* getOrLoad(std::string_view program, const ShaderDefines& defines = {});
    [[nodiscard]] rhi::IShaderProgram* getOrCompile(std::string_view program, const ShaderDefines& defines);

    // The worker retains the program. Other referenced descriptor data has static storage.
    [[nodiscard]] std::future<rhi::ComPtr<rhi::IRayTracingPipeline>>
    createRayTracingPipelineAsync(rhi::RayTracingPipelineDesc desc);

private:
    using ProgramKey = std::pair<std::string, ShaderDefines>;

    struct Variant
    {
        rhi::ComPtr<slang::ISession>     session;
        rhi::ComPtr<rhi::IShaderProgram> shader;
    };

    rhi::IShaderProgram* registerProgram(
        ProgramKey key, slang::IModule& module, const std::filesystem::path& path, std::span<const std::string> entries
    );

    rhi::IDevice*                  device = nullptr;
    std::map<ProgramKey, Variant>  variants;
    std::vector<std::future<void>> pipelineBuilds;
};
