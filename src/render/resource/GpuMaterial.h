#pragma once

#include "asset/PbrMaterialAsset.h"
#include "render/PipelineManager.h"
#include "render/resource/Buffer.h"
#include "render/resource/GpuTexture.h"
#include "render/resource/ShaderData.h"

#include <memory>
#include <span>

#include <vulkan/vulkan_raii.hpp>

class DescriptorManager;
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

        Texture                  baseColor;
        Texture                  normal;
        Texture                  metallic;
        Texture                  roughness;
        Texture                  emissive;
        Texture                  occlusion;
        std::optional<Clearcoat> clearcoat;
    };

    void create(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device&         device,
        DescriptorManager&              descriptors,
        ShaderHandle                    shader,
        const PbrMaterialAsset&         material,
        Textures                        textures
    );

    void                            updateParameters(const PbrMaterialAsset& material);
    [[nodiscard]] vk::DescriptorSet descriptorSetHandle() const { return *descriptorSet; }
    [[nodiscard]] PipelineKey
    pipelineKey(std::span<const vk::Format> colorFormats, vk::Format depthFormat, vk::PipelineLayout layout) const;
    [[nodiscard]] MaterialAlphaMode alphaMode() const { return mode; }
    [[nodiscard]] vk::ImageView     baseColorView() const { return textures.baseColor.image->imageView(); }
    [[nodiscard]] vk::Sampler       baseColorSampler() const { return textures.baseColor.sampler; }
    [[nodiscard]] const glm::vec4&  baseColorFactor() const { return parameterData.baseColorFactor; }
    [[nodiscard]] const MaterialUniforms& parameters() const { return parameterData; }
    [[nodiscard]] const Textures& textureBindings() const { return textures; }
    [[nodiscard]] bool doubleSided() const;

private:
    Textures                textures;
    std::shared_ptr<Buffer> materialBuffer;
    vk::raii::DescriptorSet descriptorSet = nullptr;
    ShaderHandle            shaderHandle;
    PipelineState           pipelineState;
    MaterialAlphaMode       mode = MaterialAlphaMode::Opaque;
    MaterialUniforms        parameterData;
};
