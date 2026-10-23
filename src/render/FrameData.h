#pragma once

#include "render/resource/Buffer.h"
#include "render/resource/ShaderData.h"

#include <span>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class Device;

class FrameData
{
public:
    void init(const VulkanContext& vulkan, Device& device);
    void reset() noexcept;
    // UBO writes follow the owning frame's fence.
    void uploadSceneUniforms(const SceneUniforms& view);
    void uploadLightUniforms(const LightUniforms& lighting);
    void writeLightBufferDescriptor(const Buffer& buffer);
    void writeShadowBufferDescriptors(std::span<const vk::DescriptorBufferInfo, 4> buffers);
    void writeShadowDescriptors(
        vk::ImageView pointShadow,
        vk::ImageView spotShadow,
        vk::ImageView rectAreaShadow,
        vk::ImageView directionalShadow,
        vk::Sampler   comparisonSampler,
        vk::Sampler   linearSampler
    );
    void writeShadowSampler(vk::Sampler sampler);
    void writeEnvironmentDescriptors(
        const vk::DescriptorImageInfo& irradiance,
        const vk::DescriptorImageInfo& prefilteredSpecular,
        const vk::DescriptorImageInfo& radiance
    );
    void writeLutDescriptors(vk::ImageView brdfLut, vk::ImageView sheenLut, vk::Sampler sampler);

    [[nodiscard]] vk::DescriptorSet sceneSetHandle() const { return *sceneSet; }
    [[nodiscard]] vk::DescriptorSet lightingSetHandle() const { return *lightingSet; }
    [[nodiscard]] vk::DescriptorSet viewSetHandle() const { return *viewSet; }

private:
    const VulkanContext*    vulkan = nullptr;
    Buffer                  sceneBuffer;
    Buffer                  lightUniformBuffer;
    vk::raii::DescriptorSet sceneSet    = nullptr;
    vk::raii::DescriptorSet lightingSet = nullptr;
    vk::raii::DescriptorSet viewSet     = nullptr;

    void writeSceneDescriptorSet();
    void writeLightUniformDescriptorSet();
    void writeEditorViewDescriptorSet();
};
