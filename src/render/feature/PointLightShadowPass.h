#pragma once

#include "render/ShaderManager.h"

#include <vulkan/vulkan_raii.hpp>

#include "RenderGraph.h"

class PipelineManager;
class VulkanContext;
class RenderGraph;

class PointLightShadowPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle pointLightShadowMap;
    };

    Output setup(RenderGraph& graph);
    void   execute(RenderGraph& graph) const;

private:
    static constexpr uint32_t kPointViewMask = 0b111111;

    RenderGraph::ImageHandle pointLightShadowMap;

    uint32_t           pointShadowMapSize = 0;
    ShaderHandle       shader;
    vk::Pipeline       pipeline       = nullptr;
    vk::PipelineLayout pipelineLayout = nullptr;
};
