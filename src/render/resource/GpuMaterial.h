#pragma once

#include "asset/PbrMaterialAsset.h"
#include "render/device/Device.h"
#include "render/resource/MaterialData.h"

#include <span>

#include <vulkan/vulkan_raii.hpp>

enum class MaterialAlphaMode
{
    Opaque,
    Mask,
    Blend
};

class AssetManager;
class ShaderManager;

class GpuMaterial
{
public:
    GpuMaterial() = default;

    struct Textures
    {
        struct Clearcoat
        {
            TextureBinding factor;
            TextureBinding roughness;
            TextureBinding normal;
        };

        TextureBinding baseColor;
        TextureBinding normal;
        TextureBinding metallic;
        TextureBinding roughness;
        TextureBinding emissive;
        TextureBinding occlusion;
        TextureBinding sheenColor;
        TextureBinding sheenRoughness;
        Clearcoat      clearcoat;
    };

    void create(const PbrMaterialAsset& material, ShaderManager& shaders, AssetManager& assets);

    void updateParameters(const PbrMaterialAsset& material);
    [[nodiscard]] GraphicsPipelineInfo
    pipelineInfo(std::span<const vk::Format> colorFormats, vk::Format depthFormat, vk::PipelineLayout layout) const;
    [[nodiscard]] MaterialAlphaMode   alphaMode() const { return mode; }
    [[nodiscard]] const MaterialData& parameters() const { return parameterData; }
    [[nodiscard]] const Textures&     textureBindings() const { return textures; }

private:
    Textures          textures;
    ShaderHandle      shaderHandle;
    PipelineState     pipelineState;
    MaterialAlphaMode mode = MaterialAlphaMode::Opaque;
    MaterialData      parameterData;
};
