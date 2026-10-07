#pragma once

#include "render/ShaderManager.h"

#include <vulkan/vulkan_raii.hpp>

#include "render/feature/RenderGraph.h"

class PipelineManager;
class VulkanContext;
class RenderGraph;

class RectAreaLightShadowPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle rectAreaLightShadowMap;
    };

    Output setup(RenderGraph& graph);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle rectAreaLightShadowMap;

    uint32_t           rectAreaShadowMapSize = 0;
    ShaderHandle       shader;
    vk::Pipeline       pipeline       = nullptr;
    vk::PipelineLayout pipelineLayout = nullptr;
};
