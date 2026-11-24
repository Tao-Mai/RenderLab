#pragma once

#include <slang-rhi.h>
#include "core/program/DefineList.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ShaderLayout;

struct ShaderHandle
{
    uint32_t index = 0;

    [[nodiscard]] bool operator==(const ShaderHandle&) const = default;
};

struct ShaderMetadata
{
    std::filesystem::path    binary;
    Falcor::DefineList       defines;
    std::vector<std::string> entries;
};

class ShaderManager
{
public:
    ShaderManager();
    ~ShaderManager();

    void init(rhi::IDevice* device);
    void shutdown() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(std::string_view program, const Falcor::DefineList& defines = {});
    [[nodiscard]] rhi::IShaderProgram*  program(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;
    [[nodiscard]] const ShaderLayout&   layout(ShaderHandle handle);

private:
    struct Variant
    {
        rhi::ComPtr<rhi::IShaderProgram> shader;
        ShaderMetadata                   metadata;
        std::unique_ptr<ShaderLayout>    layout;
    };

    rhi::IDevice*                                                      device = nullptr;
    std::map<std::pair<std::string, Falcor::DefineList>, ShaderHandle> handles;
    std::vector<Variant>                                               variants;
};
