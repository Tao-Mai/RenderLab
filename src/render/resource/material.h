#pragma once

#include "asset/asset_desc.h"
#include "render/resource/buffer.h"
#include "render/resource/texture.h"

#include <memory>

#include <vulkan/vulkan_raii.hpp>

class Material
{
public:
    Material() = default;

    void create(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device&         device,
        vk::DescriptorPool              descriptorPool,
        vk::DescriptorSetLayout         descriptorSetLayout,
        const MaterialDesc&             material,
        std::shared_ptr<Texture>        texture);

    [[nodiscard]] vk::DescriptorSet descriptorSetHandle() const;

private:
    std::shared_ptr<Texture>                 albedoTexture;
    std::shared_ptr<Buffer>                  materialBuffer;
    std::shared_ptr<vk::raii::DescriptorSet> descriptorSet;
};
