#pragma once

#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowPass.h"

#include <memory>
#include <vulkan/vulkan_raii.hpp>

class RenderResourceManager;
class GpuTexture;
class VulkanContext;
class RenderGraph;

class ScenePass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
        ShadowPass::Output       shadow;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle depthHandle, ShadowPass::Output shadow);
    void   bindResources(RenderGraph& graph) const;
    void   execute(RenderGraph& graph) const;

private:
    VulkanContext*              vulkan = nullptr;
    RenderGraph*                graph  = nullptr;
    RenderGraph::ImageHandle    color;
    RenderGraph::ImageHandle    depth;
    RenderGraph::ImageHandle    pointShadowMap;
    RenderGraph::ImageHandle    rectAreaShadowMap;
    RenderGraph::ImageHandle    cascadeShadowMap;
    RenderResourceManager*      resources = nullptr;
    std::shared_ptr<GpuTexture> irradianceTexture;
    std::shared_ptr<GpuTexture> prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture> brdfLutTexture;
    vk::Sampler                 brdfLutSampler = nullptr;
    vk::raii::Sampler           shadowSampler  = nullptr;
    vk::Filter                  shadowFilter   = vk::Filter::eLinear;

    void bindEnvironmentTextures();
};
