#pragma once

#include "render/resources/GpuShader.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

using ShaderDefines = std::map<std::string, int>;

struct ShaderHandle
{
    uint32_t index = 0;

    [[nodiscard]] bool operator==(const ShaderHandle&) const = default;
};

struct ShaderMetadata
{
    std::filesystem::path    binary;
    ShaderDefines            defines;
    std::vector<std::string> entries;
};

class ShaderManager
{
public:
    ~ShaderManager();

    void init(const vk::raii::Device& device);
    void shutdown() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(std::string_view program, const ShaderDefines& defines = {});
    [[nodiscard]] vk::ShaderModule      module(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;

private:
    const vk::raii::Device*                                       device = nullptr;
    std::map<std::pair<std::string, ShaderDefines>, ShaderHandle> handles;
    std::vector<std::unique_ptr<GpuShader>>                       shaders;
    std::vector<ShaderMetadata>                                   shaderMetadata;
};
