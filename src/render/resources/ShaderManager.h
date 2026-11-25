#pragma once

#include <slang-rhi.h>

#include <compare>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using ShaderDefines = std::map<std::string, std::string, std::less<>>;

class ShaderManager
{
public:
    [[nodiscard]] static rhi::IPersistentCache* persistentCache();

    void init(rhi::IDevice* device);
    void shutdown() noexcept;

    [[nodiscard]] rhi::IShaderProgram* getProgram(
        std::string_view                   source,
        std::vector<std::string>           entries,
        const ShaderDefines&               defines  = {},
        std::span<const std::string_view>  includes = {}
    );

private:
    struct ProgramKey
    {
        std::string source;

        std::vector<std::string> entries;

        ShaderDefines defines;

        std::vector<std::string> includes;

        auto operator<=>(const ProgramKey&) const = default;
    };

    struct CacheHeader
    {
        ProgramKey program;

        std::string module;

        std::vector<std::string> bytecode;
    };

    struct Variant
    {
        rhi::ComPtr<slang::ISession>     session;
        rhi::ComPtr<rhi::IShaderProgram> shader;
        rhi::ComPtr<ISlangBlob>          module;
    };

    rhi::IDevice*                              device = nullptr;
    std::map<ProgramKey, std::vector<Variant>> variants;
};
