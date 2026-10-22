#pragma once

#include "render/resource/Buffer.h"
#include "render/resource/MaterialData.h"

#include <array>
#include <unordered_map>
#include <vector>

class Device;
class GpuMaterial;
class RenderResourceManager;
class VulkanContext;

// One scene-wide material set per in-flight frame, updated only after its fence completes.
class MaterialTable
{
public:
    void                            begin(RenderResourceManager& resources);
    uint32_t                        add(const GpuMaterial& material);
    void                            upload(const VulkanContext& vulkan, Device& device);
    [[nodiscard]] vk::DescriptorSet descriptorSetHandle() const { return *descriptorSet; }

private:
    Buffer                                                    materialBuffer;
    vk::raii::DescriptorSet                                   descriptorSet = nullptr;
    std::vector<MaterialData>                                 materials;
    std::unordered_map<const GpuMaterial*, uint32_t>          materialIndices;
    std::vector<vk::ImageView>                                textureViews;
    std::vector<vk::Sampler>                                  samplerHandles;
    std::array<vk::DescriptorImageInfo, kMaxMaterialTextures> images;
    std::array<vk::DescriptorImageInfo, kMaxMaterialSamplers> samplers;
    std::array<vk::DescriptorImageInfo, kMaxMaterialTextures> boundImages{};
    std::array<vk::DescriptorImageInfo, kMaxMaterialSamplers> boundSamplers{};
};
