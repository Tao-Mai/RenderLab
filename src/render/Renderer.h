#pragma once

#include <slang-rhi.h>
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/editor/EditorFrameInput.h"
#include "render/graph/RenderGraph.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderManager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>


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
    RenderFrameResult render(const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds);
    void waitIdle();
    void shutdown() noexcept;

    // Apply the selection before recording the next frame.
    void switchGraph(std::string_view name);

    [[nodiscard]] rhi::IDevice&                getDevice() { return *device; }
    [[nodiscard]] rhi::ISurface*               getSurface() const { return surface; }
    [[nodiscard]] ShaderManager&               getShaderManager() { return shaders; }
    [[nodiscard]] const std::shared_ptr<RenderGraph>& getActiveGraph() const { return mpRenderGraph; }
    [[nodiscard]] bool                       supportsRayTracing() const { return device->hasFeature(rhi::Feature::RayTracing); }
    void                         renderUI(const PassEditorContext& context);
    void                         addOption(std::string_view name, nlohmann::json value);
    void                         addOptions(const nlohmann::json& options);
    void                         clearOptions();

private:
    rhi::ComPtr<rhi::ISurface>                    surface;
    rhi::ComPtr<rhi::ICommandQueue>               queue;
    rhi::ComPtr<rhi::IFence>                      frameFence;
    ShaderManager                                 shaders;
    rhi::ComPtr<rhi::IDevice>                     device;
    RenderResourceManager                         resources;
    GpuScene                                                      gpuScene;
    nlohmann::json                                appliedParameters;
    std::optional<std::string>                    pendingGraphName;
    bool                                          optionsDirty = false;
    nlohmann::ordered_json                        parameterDefaults = nlohmann::ordered_json::object();
    nlohmann::ordered_json                        parameters        = nlohmann::ordered_json::object();
    uint64_t                                                      lastSubmittedFrame = 0;
    uint32_t                                                      activeFramesInFlight = kMaxFramesInFlight;
    std::shared_ptr<RenderGraph>                  mpRenderGraph;
    rhi::ComPtr<rhi::IRenderPipeline>                             blitPipeline = nullptr;

    void                  applyConfiguration();
    void                  configureGraph(const std::string& graphName, const nlohmann::json& parameterOverrides);
    void                  initBlit();
    void                  recordPresentation(
        rhi::ICommandEncoder& commands, rhi::ITexture& backBuffer, const EditorFrameInput& editor, uint32_t frameIndex
    );
    void                  initSurface();
    void                  waitForFrame(uint64_t value);
};
