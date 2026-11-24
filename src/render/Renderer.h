#pragma once

#include <slang-rhi.h>
#include "render/PreparedScene.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/device/GpuResources.h"
#include "render/editor/EditorFrameInput.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/resources/SceneShading.h"
#include "render/raytracing/PathPipelineCache.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderManager.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>


struct SceneRenderData;
struct PassEditorContext;

enum class RenderFrameResult
{
    Presented,
    SwapchainNeedsRebuild,
};

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init();
    void resizeSurface();
    RenderFrameResult
    render(const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds);
    void waitIdle();
    void shutdown() noexcept;

    // The prepared scene is reused by the next call.
    const PreparedScene& loadSceneAssets(const SceneRenderData& scene, SceneInputs sceneInputs);

    [[nodiscard]] rhi::IDevice&                getDevice() { return *device; }
    [[nodiscard]] rhi::ISurface*               getSurface() const { return surface; }
    [[nodiscard]] ShaderManager&               getShaderManager() { return shaders; }
    [[nodiscard]] const RenderContext& getRenderContext() const { return renderContext; }
    [[nodiscard]] RenderGraph*         getActiveGraph() const { return mpRenderGraph.get(); }
    [[nodiscard]] bool                       supportsRayTracing() const { return resources.supportsRayTracing(); }
    void                         renderUI(const PassEditorContext& context);
    void                         addOption(std::string_view name, nlohmann::json value);
    void                         addOptions(const nlohmann::json& options);
    void                         clearOptions();
    [[nodiscard]] uint32_t       framesInFlight() const { return activeFramesInFlight; }

private:
    std::unique_ptr<GpuResources>                 commandResources;
    rhi::ComPtr<rhi::ISurface>                    surface;
    rhi::ComPtr<rhi::ICommandQueue>               queue;
    rhi::ComPtr<rhi::IFence>                      frameFence;
    RenderContext                         renderContext;
    ShaderManager                                 shaders;
    rhi::ComPtr<rhi::IDevice>                     device;
    RenderResourceManager                         resources;
    SceneAccelerationStructures                   sceneAccelerationStructures;
    MaterialTable                                 materialTable;
    SceneShading                                  shading;
    PathPipelineCache                     pathPipelines;
    PreparedScene                                 preparedScene;
    nlohmann::json                                appliedParameters;
    std::optional<nlohmann::json>                 pendingOptions;
    Properties::json                      parameterDefaults = Properties::json::object();
    Properties::json                      parameters        = Properties::json::object();
    std::map<std::pair<std::string, std::string>, nlohmann::json> scriptedPassProperties;
    uint64_t                                                      lastSubmittedFrame = 0;
    uint32_t                                                      activeFramesInFlight = kMaxFramesInFlight;
    ref<RenderGraph>                              mpRenderGraph;
    rhi::ComPtr<rhi::IRenderPipeline>                             blitPipeline = nullptr;

    void                  applyConfiguration();
    void                  onOptionsChange();
    void                  applyPassProperties(RenderGraph& renderGraph, const nlohmann::json& properties);
    const nlohmann::json& getScriptedPassProperties(const RenderPass& pass);
    void                  loadScript(const std::filesystem::path& path);
    void                  setActiveGraph(const ref<RenderGraph>& renderGraph);
    void                  initBlit();
    void                  recordPresentation(
        rhi::ICommandEncoder& commands, rhi::ITexture& backBuffer, const EditorFrameInput& editor
    );
    void                  initSurface();
    void                  waitForFrame(uint64_t value);
};
