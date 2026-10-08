#pragma once

#include "render/feature/RenderGraph.h"

class ShadowPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle pointLightShadowMap;
        RenderGraph::ImageHandle rectAreaLightShadowMap;
        RenderGraph::ImageHandle cascadeShadowMap;
    };

    Output setup(RenderGraph& graph);
    void   execute(RenderGraph& graph) const;

private:
    static constexpr uint32_t kPointFaceCount = 6;
    static constexpr uint32_t kPointViewMask  = 0b111111;

    struct ShadowTarget
    {
        RenderGraph::ImageHandle image;
        vk::Pipeline             pipeline       = nullptr;
        uint32_t                 mapSize        = 0;
        uint32_t                 viewMask       = 0;
        uint32_t                 viewLayerCount = 1;
    };

    ShadowTarget       point;
    ShadowTarget       rectArea;
    ShadowTarget       directional;
    uint32_t           cascadeCount   = 0;
    vk::PipelineLayout pipelineLayout = nullptr;

    void recordShadow(
        RenderGraph&        graph,
        const ShadowTarget& target,
        uint32_t            lightIndex,
        uint32_t            baseArrayLayer,
        uint32_t            cascadeIndex = 0
    ) const;
};
