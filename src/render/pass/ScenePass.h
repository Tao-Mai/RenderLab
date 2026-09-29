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

class ScenePass
{
public:
    struct Target
    {
        uint32_t     imageIndex;
        ViewportRect viewport;
        bool         storeDepthForPicking;
    };

    void init(VulkanContext& vulkan, Swapchain& swapchain,
        std::array<FrameContext, maxFramesInFlight>& frames,
        PipelineManager& pipelines,
        RenderResourceManager& resources, ShaderHandle sceneShader,
        ShaderHandle lightShader, std::shared_ptr<GpuTexture> brdfLut,
        vk::Sampler brdfLutSampler);
    void reset() noexcept;
    void refreshPipelines();
    void bindSceneTextures(const Scene::Desc& scene);
    void updateScene(
        uint32_t frameIndex,
        const glm::mat4& viewProjection,
        const glm::vec3& cameraPosition,
        const Scene::Desc::Object* lightObject);
    void record(
        vk::raii::CommandBuffer&            commandBuffer,
        const std::vector<SceneRenderItem>& renderItems,
        const std::vector<LightRenderItem>& lightRenderItems,
        const LightMarkers&                 lightMarkers,
        uint32_t                            frameIndex,
        Target                              target) const;

    [[nodiscard]] vk::DescriptorSet sceneSetHandle(uint32_t frameIndex) const;

private:
    VulkanContext*                                         vulkan    = nullptr;
    Swapchain*                                             swapchain = nullptr;
    std::array<FrameContext, maxFramesInFlight>*            frames = nullptr;
    PipelineManager*                                       pipelines = nullptr;
    RenderResourceManager*                                 resources = nullptr;
    ShaderHandle                                           sceneShader;
    ShaderHandle                                           lightShader;
    glm::vec3                                              cameraPosition{0.0f};
    vk::Pipeline                                           scenePipeline;
    vk::Pipeline                                           lightMarkerPipeline;
    vk::PipelineLayout                                     pipelineLayout;
    std::shared_ptr<GpuTexture>                              irradianceTexture;
    std::shared_ptr<GpuTexture>                              prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture>                              brdfLutTexture;
    vk::Sampler                                           brdfLutSampler = nullptr;
    float                                                 prefilteredSpecularMaxLod = 0.0f;

    void bindSceneDescriptor(
        vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) const;
};
