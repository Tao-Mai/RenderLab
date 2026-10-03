#pragma once

#include "editor/ViewportRect.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/device/FrameContext.h"
#include "render/scene/GpuScene.h"

#include <array>
#include <cstdint>
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
    enum Input { Color, Depth, Shadow, Irradiance, Prefiltered, Radiance, BrdfLut, InputCount };
    enum Output { ColorResult, DepthResult, OutputCount };

    void init(RenderGraph& graph,
        std::shared_ptr<GpuTexture> brdfLut, vk::Sampler brdfLutSampler);
    void registerPass(RenderGraph& graph);
    void setupPass(RenderGraph& graph);
    void prepareRenderData(const FrameContext& frame);
    void executePass(RenderGraph& graph) const;
    void bindSceneTextures(const SceneAsset& scene);

private:
    struct DrawCall
    {
        GpuMesh* mesh;
        const GpuMaterial* material;
        glm::mat4 model;
        uint32_t firstIndex;
        uint32_t indexCount;
        float distanceSquared;
    };
    std::vector<DrawCall> opaqueDraws;
    std::vector<DrawCall> transparentDraws;
    VulkanContext*                                         vulkan    = nullptr;
    RenderGraph*                                          graph = nullptr;
    std::vector<uint32_t> inputSlots;
    std::vector<uint32_t> outputSlots;
    PipelineManager*                                       pipelines = nullptr;
    RenderResourceManager*                                 resources = nullptr;
    ShaderHandle                                           sceneShader;
    ShaderHandle                                           lightShader;
    ShaderHandle                                           skyboxShader;
    bool                                                   hasSkybox = false;
    vk::Pipeline                                           scenePipeline;
    vk::Pipeline                                           lightMarkerPipeline;
    vk::Pipeline                                           skyboxPipeline;
    vk::PipelineLayout                                     pipelineLayout;
    vk::PipelineLayout                                     skyboxPipelineLayout;
    std::shared_ptr<GpuTexture>                              irradianceTexture;
    std::shared_ptr<GpuTexture>                              prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture>                              radianceTexture;
    std::shared_ptr<GpuTexture>                              brdfLutTexture;
    vk::Sampler                                           brdfLutSampler = nullptr;

    void recordSkybox(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex,
        const SkyboxPushConstants& parameters) const;
    void bindSceneDescriptor(
        vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) const;
};
