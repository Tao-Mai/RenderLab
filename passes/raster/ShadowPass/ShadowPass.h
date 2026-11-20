#pragma once

#include "render/graph/RenderPass.h"

#include "passes/raster/ShadowPass/ShadowSettings.h"
#include "render/graph/PassSettings.h"

#include "render/device/Device.h"

#include "render/graph/RenderGraph.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/ShaderData.h"

#include <span>
#include <vector>

class ShadowPass : public ConfigurablePass<ShadowSettings>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void   prepare(RenderGraph& graph, const PassFrame& frame) override;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    static constexpr uint32_t kPointFaceCount = 6;
    static constexpr uint32_t kPointViewMask  = 0b111111;

    struct ShadowTarget
    {
        RenderGraph::ImageHandle  image;
        RenderGraph::BufferHandle buffer;
        vk::Pipeline              pipeline       = nullptr;
        uint32_t                  mapSize        = 0;
        uint32_t                  viewMask       = 0;
        uint32_t                  viewLayerCount = 1;
    };

    struct ShadowData
    {
        std::vector<PointShadowData>       pointShadows;
        std::vector<SpotShadowData>        spotShadows;
        std::vector<RectAreaShadowData>    rectAreaShadows;
        std::vector<DirectionalShadowData> directionalShadows;
    };

    ShadowTarget              point;
    ShadowTarget              spot;
    ShadowTarget              rectArea;
    ShadowTarget              directional;
    ShadowData                shadowData;
    uint32_t                  cascadeCount = 0;
    vk::PipelineLayout        pipelineLayout;
    vk::Sampler               linearComparisonSampler;
    vk::Sampler               nearestComparisonSampler;
    std::vector<uint32_t>     visibilityMasks;

    void prepareShadowData(
        const SceneUniforms& view, std::span<const LightData> frameLights, const ShadowSettings& shadows
    );

    void recordShadow(
        RenderGraph&             graph,
        vk::raii::CommandBuffer& commandBuffer,
        const PassFrame&         execution,
        const ShadowTarget&      target,
        uint32_t                 lightIndex,
        uint32_t                 baseArrayLayer,
        uint32_t                 cascadeIndex = 0
    ) const;
};
