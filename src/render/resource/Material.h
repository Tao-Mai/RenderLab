#pragma once

#include "asset/AssetDesc.h"
#include "render/PipelineManager.h"
#include "render/resource/Buffer.h"
#include "render/resource/Texture.h"

#include <memory>

#include <vulkan/vulkan_raii.hpp>

class DescriptorManager;

class Material
{
public:
    Material() = default;

    void create(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device&         device,
        DescriptorManager&              descriptors,
        ShaderHandle                     shader,
        const MaterialDesc&             material,
        std::shared_ptr<Texture>        texture);

    [[nodiscard]] vk::DescriptorSet descriptorSetHandle() const;
    [[nodiscard]] PipelineKey pipelineKey(vk::Format colorFormat, vk::Format depthFormat) const;
    [[nodiscard]] RenderMode renderMode() const;

private:
    std::shared_ptr<Texture>                 albedoTexture;
    std::shared_ptr<Buffer>                  materialBuffer;
    vk::raii::DescriptorSet                  descriptorSet = nullptr;
    ShaderHandle                              shaderHandle;
    PipelineState                             pipelineState;
    RenderMode                                mode = RenderMode::Opaque;
};
