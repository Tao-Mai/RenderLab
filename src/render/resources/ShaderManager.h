#pragma once

#include "core/program/DefineList.h"
#include "render/resources/GpuShader.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class ShaderLayout;

struct ShaderHandle
{
    uint32_t index = 0;

    [[nodiscard]] bool operator==(const ShaderHandle&) const = default;
};

struct ShaderMetadata
{
    std::filesystem::path    binary;
    Falcor::DefineList               defines;
    std::vector<std::string> entries;
};

class ShaderManager
{
public:
    ShaderManager();
    ~ShaderManager();

    void init(const vk::raii::Device& device);
    void shutdown() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(std::string_view program, const Falcor::DefineList& defines = {});
    [[nodiscard]] vk::ShaderModule      module(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;
    [[nodiscard]] const ShaderLayout&   layout(ShaderHandle handle);

private:
    struct Variant
    {
        std::unique_ptr<GpuShader> shader;
        ShaderMetadata metadata;
        std::unique_ptr<ShaderLayout> layout;
    };

    const vk::raii::Device*                                       device = nullptr;
    std::map<std::pair<std::string, Falcor::DefineList>, ShaderHandle> handles;
    std::vector<Variant> variants;
};
