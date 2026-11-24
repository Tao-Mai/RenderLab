#pragma once

#include <slang-rhi.h>
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/FrameContext.h"
#include "render/device/GpuResources.h"
#include "render/editor/EditorFrameInput.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/offline/OfflineTracePipelineCache.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>


struct SceneRenderData;
struct PassEditorContext;
class Profiler;

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

    const PreparedScene& prepare(const RenderView& view, const SceneRenderData& scene, uint32_t targetFrameIndex);

    [[nodiscard]] rhi::IDevice&                getDevice() { return *device; }
    [[nodiscard]] rhi::ISurface*               getSurface() const { return surface; }
    [[nodiscard]] ShaderManager&               getShaderManager() { return shaders; }
    [[nodiscard]] const Falcor::RenderContext& getRenderContext() const { return renderContext; }
    [[nodiscard]] Falcor::RenderGraph*         getActiveGraph() const { return mpRenderGraph.get(); }
    [[nodiscard]] bool           supportsObjectPicking() const { return static_cast<bool>(pickingOutput); }
    [[nodiscard]] ExposureConfig defaultExposure() const;
    void                         renderUI(const PassEditorContext& context);
    void                         addOption(std::string_view name, nlohmann::json value);
    void                         addOptions(const nlohmann::json& options);
    void                         clearOptions();
    [[nodiscard]] uint32_t       framesInFlight() const { return activeFramesInFlight; }

private:
    std::unique_ptr<GpuResources>                 commandResources;
    rhi::ComPtr<rhi::ISurface>                    surface;
    rhi::ComPtr<rhi::ICommandQueue>               queue;
    Falcor::RenderContext                         renderContext;
    ShaderManager                                 shaders;
    rhi::ComPtr<rhi::IDevice>                     device;
    RenderResourceManager                         resources;
    SceneAccelerationStructures                   sceneAccelerationStructures;
    MaterialTable                                 materialTable;
    OfflineScene                                  offlineScene;
    OfflineTracePipelineCache                     offlinePipelines;
    std::array<FrameContext, kMaxFramesInFlight>  frames;
    std::array<FrameData, kMaxFramesInFlight>     frameStorage;
    std::array<PreparedScene, kMaxFramesInFlight> renderStorage;
    std::optional<EnvironmentMapAsset::ID>        environmentMap;
    rhi::ComPtr<rhi::ITexture>                    radianceTexture;
    rhi::ComPtr<rhi::ITexture>                    irradianceTexture;
    rhi::ComPtr<rhi::ITexture>                    prefilteredSpecularTexture;
    rhi::ComPtr<rhi::ITexture>                    brdfLutTexture;
    rhi::ComPtr<rhi::ITexture>                    sheenLutTexture;
    float                                         prefilteredMaxLod = 0.0f;
    const RendererConfig*                         rendererConfig    = nullptr;
    nlohmann::json                                appliedParameters;
    std::optional<nlohmann::json>                 pendingOptions;
    Falcor::Properties::json                      parameterDefaults = Falcor::Properties::json::object();
    Falcor::Properties::json                      parameters        = Falcor::Properties::json::object();
    std::map<std::pair<std::string, std::string>, nlohmann::json> scriptedPassProperties;
    uint32_t                                                      frameIndex           = 0;
    uint32_t                                                      activeFramesInFlight = kMaxFramesInFlight;
    Falcor::ref<Falcor::RenderGraph>                              mpRenderGraph;
    Falcor::ref<Falcor::RenderPass>                               exposurePass;
    rhi::ComPtr<rhi::IBuffer>                                     lightResource;
    rhi::ComPtr<rhi::ITexture>                                    pickingOutput;
    rhi::ComPtr<rhi::IBuffer>                                     pickingReadback;
    GpuResources::BufferHandle                                    lightBuffer;
    rhi::ComPtr<rhi::IRenderPipeline>                             blitPipeline = nullptr;

    void                  applyConfiguration();
    void                  onOptionsChange();
    void                  applyPassProperties(Falcor::RenderGraph& renderGraph, const nlohmann::json& properties);
    const nlohmann::json& getScriptedPassProperties(const Falcor::RenderPass& pass);
    void                  loadScript(const std::filesystem::path& path);
    void                  setActiveGraph(const Falcor::ref<Falcor::RenderGraph>& renderGraph);
    void                  initFrameResources();
    void                  initBlit();
    void                  endFrame(Falcor::RenderContext& context, rhi::ITexture* backBuffer);
    void                  initSurface();
    void                  initLightingResources();
    void                  updateEnvironmentBindings(const std::optional<EnvironmentMapAsset::ID>& id);
    void                  bindEnvironmentTextures();

    [[nodiscard]] FrameContext& currentFrame() { return frames[frameIndex]; }
};
