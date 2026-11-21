#pragma once

#include "render/device/VulkanResources.h"
#include "render/resources/ShaderData.h"

#include <span>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
namespace Falcor
{
class Device;
}  // namespace Falcor

class FrameData
{
public:
    void init(const VulkanContext& vulkan, Falcor::Device& device);
    void reset() noexcept;
    // UBO writes follow the owning frame's fence.
    void uploadSceneUniforms(const SceneUniforms& view);
    void uploadLightUniforms(const LightUniforms& lighting);
    void uploadShadowUniforms(const ShadowUniforms& shadows);
    void writeLightBufferDescriptor(VulkanResources& resources, VulkanResources::BufferHandle buffer);
    void writeShadowSampler(vk::Sampler sampler);
    void writeEnvironmentDescriptors(
        const vk::DescriptorImageInfo& irradiance,
        const vk::DescriptorImageInfo& prefilteredSpecular,
        const vk::DescriptorImageInfo& radiance
    );
    void writeLutDescriptors(vk::ImageView brdfLut, vk::ImageView sheenLut, vk::Sampler sampler);

    [[nodiscard]] vk::DescriptorSet sceneSetHandle() const { return *sceneSet; }
    [[nodiscard]] vk::DescriptorSet lightSetHandle() const { return *lightSet; }
    [[nodiscard]] vk::DescriptorSet shadowSetHandle() const { return *shadowSet; }
    [[nodiscard]] vk::DescriptorSet viewSetHandle() const { return *viewSet; }
    [[nodiscard]] VulkanResources::BufferHandle lightBufferHandle() const { return lightBuffer; }

private:
    const VulkanContext*    vulkan = nullptr;
    Falcor::Buffer                  sceneBuffer;
    Falcor::Buffer                  lightUniformBuffer;
    VulkanResources::BufferHandle lightBuffer;
    Falcor::Buffer                  shadowUniformBuffer;
    vk::raii::DescriptorSet sceneSet    = nullptr;
    vk::raii::DescriptorSet lightSet = nullptr;
    vk::raii::DescriptorSet shadowSet = nullptr;
    vk::raii::DescriptorSet viewSet     = nullptr;
    vk::Sampler             shadowComparisonSampler;

    void writeSceneDescriptorSet();
    void writeLightUniformDescriptorSet();
    void writeShadowUniformDescriptorSet();
    void writeEditorViewDescriptorSet();
};
