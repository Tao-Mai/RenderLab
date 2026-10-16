#pragma once

#include "render/raytracing/RayTracingData.h"

#include <array>
#include <map>
#include <vector>
#include <vulkan/vulkan.hpp>

class Buffer;
class GpuMaterial;
class RenderResourceManager;
class VulkanContext;

// Per-dispatch material table shared by realtime and offline ray tracing.
class RayTracingMaterials
{
public:
    explicit RayTracingMaterials(RenderResourceManager& resources);

    uint32_t add(const GpuMaterial& material);
    void     upload(Buffer& buffer, const VulkanContext& vulkan) const;

    std::array<vk::DescriptorImageInfo, kMaxRayTracingTextures> images;
    std::array<vk::DescriptorImageInfo, kMaxRayTracingSamplers> samplers;

private:
    std::vector<RayTracingMaterialData>    materials;
    std::map<const GpuMaterial*, uint32_t> materialIndices;
    std::vector<vk::ImageView>             textureViews;
    std::vector<vk::Sampler>               samplerHandles;
};
