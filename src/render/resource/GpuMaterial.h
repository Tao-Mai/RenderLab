#pragma once

#include "asset/Asset.h"
#include "render/PipelineManager.h"
#include "render/resource/Buffer.h"
#include "render/resource/GpuTexture.h"

#include <memory>
#include <span>

#include <vulkan/vulkan_raii.hpp>

class DescriptorManager;

class GpuMaterial
{
public:
    GpuMaterial() = default;

    struct Texture
    {
        std::shared_ptr<GpuTexture> image;
        vk::Sampler sampler;
    };

    struct Textures
    {
        struct Clearcoat
        {
            Texture factor;
            Texture roughness;
            Texture normal;
        };

        Texture baseColor;
        Texture normal;
        Texture metallic;
        Texture roughness;
        Texture emissive;
        Texture occlusion;
        std::optional<Clearcoat> clearcoat;
    };

    void create(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device&         device,
        DescriptorManager&            descriptors,
        ShaderHandle                  shader,
        const MaterialAsset&          material,
        Textures                      textures);

    [[nodiscard]] vk::DescriptorSet descriptorSetHandle() const;
    [[nodiscard]] PipelineKey pipelineKey(std::span<const vk::Format> colorFormats, vk::Format depthFormat) const;
    [[nodiscard]] RenderMode renderMode() const;
    [[nodiscard]] vk::ImageView baseColorView() const { return textures.baseColor.image->imageView(); }
    [[nodiscard]] vk::Sampler baseColorSampler() const { return textures.baseColor.sampler; }
    [[nodiscard]] const glm::vec4& baseColorFactor() const { return colorFactor; }

private:
    Textures                textures;
    std::shared_ptr<Buffer>  materialBuffer;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    ShaderHandle            shaderHandle;
    PipelineState           pipelineState;
    RenderMode              mode = RenderMode::Opaque;
    glm::vec4               colorFactor{1.0f};
};
