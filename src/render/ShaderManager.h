#pragma once

#include "asset/ShaderAsset.h"
#include "render/resource/GpuShader.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class AssetManager;

using ShaderDefines = std::map<std::string, int>;

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
        uint32_t             count = 1;
    };

    ShaderAsset::ID          id;
    std::filesystem::path    binary;
    std::vector<Binding>     bindings;
    ShaderDefines            defines;
    std::vector<std::string> entries;
    uint32_t                 pushConstantSize = 0;
};

class ShaderManager
{
public:
    ~ShaderManager();

    void init(const vk::raii::Device& device, AssetManager& assets);
    void shutdown() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(const ShaderAsset::ID& id, const ShaderDefines& defines = {});
    [[nodiscard]] vk::ShaderModule      module(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;

private:
    const vk::raii::Device*                                       device = nullptr;
    AssetManager*                                                 assets = nullptr;
    std::map<std::pair<std::string, ShaderDefines>, ShaderHandle> handles;
    std::vector<std::unique_ptr<GpuShader>>                       shaders;
    std::vector<ShaderMetadata>                                   shaderMetadata;
};
