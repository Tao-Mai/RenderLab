#pragma once

#include "core/Debug.h"

#include "render/DescriptorManager.h"
#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/ShaderManager.h"
#include "render/device/FrameContext.h"
#include "render/device/VulkanContext.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/editor/EditorFrameInput.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct SceneRenderData;
class SceneBindings;

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void              init();
    void              rebuildGraph(const SceneRenderData& scene);
    void              recreateSwapchain(const SceneRenderData& scene);
    RenderFrameResult render(const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor);
    void              waitIdle();
    void              shutdown() noexcept;

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
    DescriptorManager                           descriptors;
    PipelineManager                             pipelines;
    RenderResourceManager                       resources;
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    frameStorage;
    SceneBindings*                              activeSceneBindings = nullptr;
    std::optional<RendererConfig>               appliedConfig;
    struct SceneOutput
    {
        RenderGraph::ImageHandle color;
        RenderGraph::ImageHandle depth;
    };
    SceneOutput               setupRasterization(const SceneRenderData& scene);
    SceneOutput               setupRayTracing();
    SceneOutput               setupOffline();
    RenderGraph::ImageHandle  setupPostProcess(RenderGraph::ImageHandle color);
    void                      setupEditor(SceneOutput scene);
    void                      applyConfiguration(const SceneRenderData& scene);
    const RenderData&         prepare(const RenderView& view, const SceneRenderData& scene);
    uint32_t                  frameIndex           = 0;
    uint32_t                  activeFramesInFlight = maxFramesInFlight;
    RenderGraph               graph;
    std::unique_ptr<Passes>   passes;
    RenderGraph::BufferHandle pickingReadback;
    void                      setupRenderGraph(const SceneRenderData& scene);

    template <class T, class... Args>
    auto setupPass(Args&&... args);

    [[nodiscard]] FrameContext& currentFrame() { return frames[frameIndex]; }
};
