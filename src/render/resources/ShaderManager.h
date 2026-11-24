#pragma once

#include <slang-rhi.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ShaderLayout;

using ShaderDefines = std::map<std::string, std::string>;

struct ShaderHandle
{
    uint32_t index = 0;

    [[nodiscard]] bool operator==(const ShaderHandle&) const = default;
};

struct ShaderMetadata
{
    std::filesystem::path    binary;  // Empty for source-compiled programs.
    ShaderDefines            defines;
    std::vector<std::string> entries;
};

class ShaderManager
{
public:
    ShaderManager();
    ~ShaderManager();

    void init(rhi::IDevice* device);
    void shutdown() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(std::string_view program, const ShaderDefines& defines = {});
    [[nodiscard]] ShaderHandle          getOrCompile(std::string_view program, const ShaderDefines& defines);
    [[nodiscard]] rhi::IShaderProgram*  program(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;
    [[nodiscard]] const ShaderLayout&   layout(ShaderHandle handle);

private:
    using ProgramKey = std::pair<std::string, ShaderDefines>;

    struct Variant
    {
        rhi::ComPtr<slang::ISession>     session;
        rhi::ComPtr<rhi::IShaderProgram> shader;
        ShaderMetadata                   metadata;
        std::unique_ptr<ShaderLayout>    layout;
    };

    ShaderHandle registerProgram(
        const ProgramKey& key, slang::IModule& module, const std::filesystem::path& path, ShaderMetadata metadata
    );

    rhi::IDevice*                      device = nullptr;
    std::map<ProgramKey, ShaderHandle> handles;
    std::vector<Variant>               variants;
};
