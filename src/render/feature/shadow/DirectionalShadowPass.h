//
// Created by 24500 on 2026/10/8.
//

#pragma once

#include "render/ShaderManager.h"

#include <vulkan/vulkan_raii.hpp>

#include "render/feature/RenderGraph.h"

class DirectionalShadowPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle cascadeShadowMap;
    };

    Output setup(RenderGraph& graph);
    void   execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle cascadeShadowMap;

    uint32_t cascadeShadowMapSize = 0;
    uint32_t cascadeCount         = 0;

    ShaderHandle       shader;
    vk::Pipeline       pipeline       = nullptr;
    vk::PipelineLayout pipelineLayout = nullptr;
};
