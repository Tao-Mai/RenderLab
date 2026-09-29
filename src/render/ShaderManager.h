#pragma once

#include "asset/Asset.h"
#include "render/resource/GpuShader.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class AssetDescManager;

struct ShaderHandle
{
    uint32_t index = 0;

    [[nodiscard]] bool operator==(const ShaderHandle&) const = default;
};

struct ShaderMetadata
{
    struct Binding
    {
        uint32_t             set;
        uint32_t             binding;
        vk::DescriptorType   type;
        vk::ShaderStageFlags stages;
    };

    Shader::ID            id;
    std::filesystem::path binary;
    std::vector<Binding>  bindings;
};

class ShaderManager
{
public:
    ~ShaderManager();

    void init(const vk::raii::Device& device);
    void reset() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(const Shader::ID& id);
    [[nodiscard]] vk::ShaderModule      module(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;

private:
    const vk::raii::Device*                      device = nullptr;
    AssetDescManager*                            assets = nullptr;
    std::unordered_map<Shader::ID, ShaderHandle> handles;
    std::vector<std::unique_ptr<GpuShader>>      shaders;
    std::vector<ShaderMetadata>                  shaderMetadata;
};
