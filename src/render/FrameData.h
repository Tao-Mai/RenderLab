#pragma once

#include "render/feature/shadow/ShadowData.h"
#include "render/resource/Buffer.h"
#include "render/resource/ShaderData.h"

#include <span>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class Device;

class FrameData
{
public:
    void init(const VulkanContext& vulkan, Device& device);
    void reset() noexcept;
    void updateView(const ViewUniforms& view);
    // Update only after the owning frame's fence completes.
    void update(const ViewUniforms& view, const LightUniforms& lighting, ShadowFrameData prepared);

    [[nodiscard]] const ViewUniforms&                 viewUniforms() const { return viewData; }
    [[nodiscard]] const LightUniforms&                lightUniforms() const { return lightMetadata; }
    [[nodiscard]] std::span<const LightData>          lightData() const { return shadowData.lights; }
    [[nodiscard]] std::span<const PointShadowData>    pointShadowData() const { return shadowData.pointShadows; }
    [[nodiscard]] std::span<const SpotShadowData>     spotShadowData() const { return shadowData.spotShadows; }
    [[nodiscard]] std::span<const RectAreaShadowData> rectAreaShadowData() const { return shadowData.rectAreaShadows; }
    [[nodiscard]] std::span<const DirectionalShadowData> directionalShadowData() const
    {
        return shadowData.directionalShadows;
    }
    [[nodiscard]] vk::DescriptorSet sceneSetHandle() const { return *sceneSet; }
    [[nodiscard]] vk::DescriptorSet viewSetHandle() const { return *viewSet; }
    [[nodiscard]] const Buffer&     viewUniformBuffer() const { return viewBuffer; }

private:
    const VulkanContext*    vulkan = nullptr;
    ShadowFrameData         shadowData;
    ViewUniforms            viewData;
    LightUniforms           lightMetadata;
    Buffer                  viewBuffer;
    Buffer                  lightUniformBuffer;
    Buffer                  lightBuffer;
    Buffer                  pointShadowBuffer;
    Buffer                  spotShadowBuffer;
    Buffer                  rectAreaShadowBuffer;
    Buffer                  directionalShadowBuffer;
    vk::raii::DescriptorSet sceneSet = nullptr;
    vk::raii::DescriptorSet viewSet  = nullptr;

    void bindBuffers();
};
