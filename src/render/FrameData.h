#pragma once

#include "render/resource/Buffer.h"
#include "render/resource/ShaderData.h"

#include <span>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class DescriptorManager;

class FrameData
{
public:
    void init(const VulkanContext& vulkan, DescriptorManager& descriptors);
    void reset() noexcept;
    // Update only after the owning frame's fence completes.
    void update(const ViewUniforms& view, const LightUniforms& lighting,
        std::span<const LightData> lights, bool shadowsEnabled);

    [[nodiscard]] const ViewUniforms& viewUniforms() const;
    [[nodiscard]] const LightUniforms& lightUniforms() const;
    [[nodiscard]] std::span<const LightData> lightData() const;
    [[nodiscard]] std::span<const PointShadowData> pointShadowData() const;
    [[nodiscard]] vk::DescriptorSet sceneSetHandle() const;

private:
    const VulkanContext* vulkan = nullptr;
    ViewUniforms viewData;
    LightUniforms lightMetadata;
    std::vector<LightData> lights;
    std::vector<PointShadowData> pointShadows;
    Buffer viewBuffer;
    Buffer lightUniformBuffer;
    Buffer lightBuffer;
    Buffer pointShadowBuffer;
    vk::raii::DescriptorSet sceneSet = nullptr;

    void bindBuffers();
};
