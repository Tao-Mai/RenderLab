#pragma once

#include "asset/Asset.h"
#include "render/resource/GpuShader.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class AssetManager;

enum class ShaderVariant { Default, NoIbl, Clearcoat, ClearcoatNoIbl, RectAreaShadow, DirectionalShadow };

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

    ShaderAsset::ID            id;
    std::filesystem::path binary;
    std::vector<Binding>  bindings;
    ShaderVariant variant = ShaderVariant::Default;
};

class ShaderManager
{
public:
    ~ShaderManager();

    void init(const vk::raii::Device& device);
    void shutdown() noexcept;

    [[nodiscard]] ShaderHandle          getOrLoad(const ShaderAsset::ID& id,
        ShaderVariant variant = ShaderVariant::Default);
    [[nodiscard]] vk::ShaderModule      module(ShaderHandle handle) const;
    [[nodiscard]] const ShaderMetadata& metadata(ShaderHandle handle) const;

private:
    const vk::raii::Device*                      device = nullptr;
    AssetManager*                            assets = nullptr;
    std::map<std::pair<std::string, ShaderVariant>, ShaderHandle> handles;
    std::vector<std::unique_ptr<GpuShader>>      shaders;
    std::vector<ShaderMetadata>                  shaderMetadata;
};
