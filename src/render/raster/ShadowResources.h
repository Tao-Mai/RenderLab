#pragma once

#include "render/device/VulkanResources.h"

#include <span>

class FrameData;
class RenderResourceManager;

struct ShadowResources
{
    VulkanResources::ImageHandle  pointLightShadowMap;
    VulkanResources::ImageHandle  spotLightShadowMap;
    VulkanResources::ImageHandle  rectAreaLightShadowMap;
    VulkanResources::ImageHandle  cascadeShadowMap;
    VulkanResources::BufferHandle pointShadowBuffer;
    VulkanResources::BufferHandle spotShadowBuffer;
    VulkanResources::BufferHandle rectAreaShadowBuffer;
    VulkanResources::BufferHandle directionalShadowBuffer;
};

void bindShadowResources(
    VulkanResources& cache, const ShadowResources& shadows, std::span<FrameData> frames, RenderResourceManager& resources
);
