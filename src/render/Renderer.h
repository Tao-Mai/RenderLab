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
#include "render/graph/EndPass.h"
#include "render/graph/PythonGraph.h"
#include "render/graph/RenderGraph.h"
#include "render/graph/RenderGraphCompiler.h"
#include "render/plugins/PluginManager.h"
#include "render/editor/EditorFrameInput.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/device/Swapchain.h"
#include "render/resources/MaterialTable.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/offline/OfflineTracePipelineCache.h"
#include "render/resources/RenderResourceManager.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct SceneRenderData;
struct RenderGraphDefinition;
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
    RenderFrameResult render(
        const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds
    );
    void              waitIdle();
    void              shutdown() noexcept;

    const RenderData& prepare(const RenderView& view, const SceneRenderData& scene, uint32_t targetFrameIndex);

    [[nodiscard]] VulkanContext& vulkanContext() { return vulkan; }
    [[nodiscard]] Swapchain&     swapchainHandle() { return swapchain; }
    [[nodiscard]] const PluginManager& plugins() const { return pluginManager; }
    [[nodiscard]] const RenderGraphCompiler& graphPasses() const { return graphCompiler; }
    [[nodiscard]] const RenderGraphDefinition* activeRenderGraph() const { return graphDefinition; }
    [[nodiscard]] bool showObjectList() const { return graphShowsObjectList; }
    [[nodiscard]] bool supportsObjectPicking() const { return pickingReadback.id != RenderGraph::kInvalidHandle; }
    [[nodiscard]] std::span<GraphParameterControl> graphEditorControls() { return editorControls; }
    [[nodiscard]] ExposureConfig defaultExposure() const;
    void setPassOverrides(std::string_view type, std::string_view instance, nlohmann::json properties);
    [[nodiscard]] uint32_t       framesInFlight() const { return activeFramesInFlight; }

private:
    DEBUG_ONLY(bool inited = false;)
    VulkanContext                               vulkan;
    Swapchain                                   swapchain;
    ShaderManager                               shaders;
    Device                                      device;
    RenderResourceManager                       resources;
    SceneAccelerationStructures                 sceneAccelerationStructures;
    MaterialTable                               materialTable;
    OfflineScene                                offlineScene;
    OfflineTracePipelineCache                   offlinePipelines;
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    frameStorage;
    std::array<RenderData, maxFramesInFlight>   renderStorage;
    std::optional<EnvironmentMapAsset::ID>      environmentMap;
    std::shared_ptr<GpuTexture>                 radianceTexture;
    std::shared_ptr<GpuTexture>                 irradianceTexture;
    std::shared_ptr<GpuTexture>                 prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture>                 brdfLutTexture;
    std::shared_ptr<GpuTexture>                 sheenLutTexture;
    float                                       prefilteredMaxLod = 0.0f;
    const RendererConfig*                       rendererConfig    = nullptr;
    const RenderGraphDefinition* graphDefinition = nullptr;
    uint64_t appliedParameterRevision = 0;
    bool passOverridesChanged = false;
    bool graphShowsObjectList = false;
    std::vector<GraphParameterControl> editorControls;
    uint32_t                     frameIndex           = 0;
    uint32_t                     activeFramesInFlight = maxFramesInFlight;
    PluginManager                pluginManager;
    RenderGraphCompiler          graphCompiler;
    EndPass                      endPass;
    RenderGraph                  graph;
    RenderGraph::BufferHandle    pickingReadback;
    RenderGraph::BufferHandle    lightBuffer;
    RenderGraph::ImageHandle     presentTarget;

    void                      applyConfiguration();
    void                      setupRenderGraph();
    void                      initLightingResources();
    void                      updateEnvironmentBindings(const std::optional<EnvironmentMapAsset::ID>& id);
    void                      bindEnvironmentTextures();

    [[nodiscard]] FrameContext& currentFrame() { return frames[frameIndex]; }
};
