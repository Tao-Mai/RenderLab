#pragma once

#include "core/Debug.h"

#include "render/FrameData.h"
#include "render/RenderConfig.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/resources/ShaderManager.h"
#include "render/device/Device.h"
#include "render/device/FrameContext.h"
#include "render/device/VulkanContext.h"
#include "render/graph/RenderGraph.h"
#include "render/editor/EditorFrameInput.h"
#include "render/raster/ShadowPass.h"
#include "render/device/Swapchain.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct SceneRenderData;
class Profiler;

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void              init();
    void              rebuildGraph();
    void              recreateSwapchain();
    RenderFrameResult render(const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor);
    void              waitIdle();
    void              shutdown() noexcept;

    const RenderData& prepare(const RenderView& view, const SceneRenderData& scene, uint32_t targetFrameIndex);

    [[nodiscard]] VulkanContext& vulkanContext() { return vulkan; }
    [[nodiscard]] Swapchain&     swapchainHandle() { return swapchain; }
    [[nodiscard]] uint32_t       offlineSampleCount() const;
    [[nodiscard]] uint32_t       framesInFlight() const { return activeFramesInFlight; }

private:
    struct Passes;
    DEBUG_ONLY(bool inited = false;)
    VulkanContext                               vulkan;
    Swapchain                                   swapchain;
    ShaderManager                               shaders;
    Device                                      device;
    RenderResourceManager                       resources;
    MaterialTable                               materialTable;
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    frameStorage;
    std::array<RenderData, maxFramesInFlight>   renderStorage;
    std::optional<EnvironmentMapAsset::ID>      environmentMap;
    std::shared_ptr<GpuTexture>                 radianceTexture;
    std::shared_ptr<GpuTexture>                 irradianceTexture;
    std::shared_ptr<GpuTexture>                 prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture>                 brdfLutTexture;
    std::shared_ptr<GpuTexture>                 sheenLutTexture;
    vk::raii::Sampler                           shadowSampler     = nullptr;
    float                                       prefilteredMaxLod = 0.0f;
    const RendererConfig*                       rendererConfig    = nullptr;
    std::optional<RendererConfig>               appliedConfig;
    struct SceneOutput
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
        ShadowPass::Output       shadow;
    };
    SceneOutput               setupRasterization(RenderGraph::ImageHandle target);
    SceneOutput               setupRayTracing();
    SceneOutput               setupOffline();
    SceneOutput               setupLightMarkers(SceneOutput scene);
    RenderGraph::ImageHandle  setupPostProcess(RenderGraph::ImageHandle color, RenderGraph::ImageHandle target);
    void                      setupEditor(SceneOutput scene);
    void                      applyConfiguration();
    uint32_t                  frameIndex           = 0;
    uint32_t                  activeFramesInFlight = maxFramesInFlight;
    RenderGraph               graph;
    std::unique_ptr<Passes>   passes;
    RenderGraph::BufferHandle pickingReadback;
    RenderGraph::BufferHandle lightBuffer;
    RenderGraph::ImageHandle  presentTarget;
    void                      setupRenderGraph();
    void                      initLightingResources();
    void                      createShadowSampler();
    void                      writeShadowDescriptors(ShadowPass::Output shadow);
    void                      updateEnvironmentBindings(const std::optional<EnvironmentMapAsset::ID>& id);
    void                      bindEnvironmentTextures();

    template <class T, class... Args>
    auto setupPass(Args&&... args);

    [[nodiscard]] FrameContext& currentFrame() { return frames[frameIndex]; }
};
