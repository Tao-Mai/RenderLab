#pragma once

#include "asset/PbrMaterialAsset.h"
#include "render/device/Device.h"
#include "render/resource/GpuTexture.h"
#include "render/resource/MaterialData.h"

#include <memory>
#include <span>

#include <vulkan/vulkan_raii.hpp>

enum class MaterialAlphaMode
{
    Opaque,
    Mask,
    Blend
};

class GpuMaterial
{
public:
    GpuMaterial() = default;

    struct Texture
    {
        std::shared_ptr<GpuTexture> image;
        vk::Sampler                 sampler;
    };

    struct Textures
    {
        struct Clearcoat
        {
            Texture factor;
            Texture roughness;
            Texture normal;
        };

        Texture   baseColor;
        Texture   normal;
        Texture   metallic;
        Texture   roughness;
        Texture   emissive;
        Texture   occlusion;
        Texture   sheenColor;
        Texture   sheenRoughness;
        Clearcoat clearcoat;
    };

    void create(ShaderHandle shader, const PbrMaterialAsset& material, Textures textures);

    void updateParameters(const PbrMaterialAsset& material);
    [[nodiscard]] GraphicsPipelineInfo
    pipelineInfo(std::span<const vk::Format> colorFormats, vk::Format depthFormat, const PipelineLayout& layout) const;
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
