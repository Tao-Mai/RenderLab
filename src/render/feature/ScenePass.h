#pragma once

#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/feature/PointShadowPass.h"
#include "render/feature/RenderGraph.h"

#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class LightMarkers;
class GpuMesh;
class RenderResourceManager;
class Swapchain;
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
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle, PointShadowPass::Output shadow);
    void   bindResources(RenderGraph& graph) const;
    void   execute(RenderGraph& graph) const;

private:
    VulkanContext*              vulkan = nullptr;
    RenderGraph*                graph  = nullptr;
    RenderGraph::ImageHandle    color;
    RenderGraph::ImageHandle    depth;
    RenderGraph::ImageHandle    pointShadowMap;
    PipelineManager*            pipelines = nullptr;
    RenderResourceManager*      resources = nullptr;
    ShaderHandle                sceneShader;
    ShaderHandle                lightShader;
    ShaderHandle                skyboxShader;
    bool                        hasSkybox  = false;
    bool                        iblEnabled = false;
    vk::Pipeline                scenePipeline;
    vk::Pipeline                lightMarkerPipeline;
    vk::Pipeline                skyboxPipeline;
    vk::PipelineLayout          pipelineLayout;
    vk::PipelineLayout          skyboxPipelineLayout;
    std::shared_ptr<GpuTexture> irradianceTexture;
    std::shared_ptr<GpuTexture> prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture> radianceTexture;
    std::shared_ptr<GpuTexture> brdfLutTexture;
    vk::Sampler                 brdfLutSampler = nullptr;
    vk::raii::Sampler           shadowSampler  = nullptr;

    void bindEnvironmentTextures();
    void recordSkybox(
        vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex, const SkyboxPushConstants& parameters
    ) const;
    void bindSceneDescriptor(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) const;
};
