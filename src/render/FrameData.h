#pragma once

#include "render/RendererConfig.h"
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
    void updateView(const ViewUniforms& view);
    // Update only after the owning frame's fence completes.
    void update(
        const ViewUniforms&          view,
        const LightUniforms&         lighting,
        std::span<const LightData>   lights,
        const RenderFeature::Shadow& shadows
    );

    [[nodiscard]] const ViewUniforms&                    viewUniforms() const;
    [[nodiscard]] const LightUniforms&                   lightUniforms() const;
    [[nodiscard]] std::span<const LightData>             lightData() const;
    [[nodiscard]] std::span<const PointShadowData>       pointShadowData() const;
    [[nodiscard]] std::span<const RectAreaShadowData>    rectAreaShadowData() const { return rectAreaShadows; }
    [[nodiscard]] std::span<const DirectionalShadowData> directionalShadowData() const { return directionalShadows; }
    [[nodiscard]] vk::DescriptorSet                      sceneSetHandle() const;
    [[nodiscard]] vk::DescriptorSet                      viewSetHandle() const { return *viewSet; }

private:
    const VulkanContext*               vulkan = nullptr;
    ViewUniforms                       viewData;
    LightUniforms                      lightMetadata;
    std::vector<LightData>             lights;
    std::vector<PointShadowData>       pointShadows;
    std::vector<RectAreaShadowData>    rectAreaShadows;
    std::vector<DirectionalShadowData> directionalShadows;
    Buffer                             viewBuffer;
    Buffer                             lightUniformBuffer;
    Buffer                             lightBuffer;
    Buffer                             pointShadowBuffer;
    Buffer                             rectAreaShadowBuffer;
    Buffer                             directionalShadowBuffer;
    vk::raii::DescriptorSet            sceneSet = nullptr;
    vk::raii::DescriptorSet            viewSet = nullptr;

    void bindBuffers();
};
