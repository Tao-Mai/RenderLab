#pragma once

#include <slang-rhi.h>
#include "core/Config.h"
#include "core/utils/NonCopyable.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/graph/RenderGraph.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderManager.h"

#include <cstdint>
#include <functional>
#include <memory>


struct Scene;
struct PassEditorContext;

enum class RenderFrameResult
{
    Presented,
    SwapchainNeedsRebuild,
};

class Renderer : private NonCopyable
{
public:
    Renderer();
    ~Renderer();


    void init();
    void resizeSurface();
    RenderFrameResult render(
        const Scene& scene, uint64_t sceneGeneration, const rhi::Viewport& displayViewport,
        const std::function<void(rhi::IRenderPassEncoder&, uint32_t)>& recordUi, float deltaSeconds
    );
    void waitIdle();
    void shutdown() noexcept;

    [[nodiscard]] rhi::IDevice&                getDevice() { return *device; }
    [[nodiscard]] rhi::ISurface*               getSurface() const { return surface; }
    [[nodiscard]] ShaderManager&               getShaderManager() { return shaders; }
    void                         renderUI(const PassEditorContext& context);

private:
    rhi::ComPtr<rhi::ISurface>                    surface;
    rhi::ComPtr<rhi::ICommandQueue>               queue;
    rhi::ComPtr<rhi::IFence>                      frameFence;
    ShaderManager                                 shaders;
    rhi::ComPtr<rhi::IDevice>                     device;
    RenderResourceManager                         resources;
    GpuScene                                                      gpuScene;
    RenderGraphType                               appliedGraphType = RenderGraphType::Minimal;
    RenderGraphSettings                           appliedGraphSettings;
    RenderPassOptions                             appliedPasses;
    uint64_t                                                      lastSubmittedFrame = 0;
    uint32_t                                                      activeFramesInFlight = kMaxFramesInFlight;
    std::shared_ptr<RenderGraph>                  mpRenderGraph;
    rhi::ComPtr<rhi::IRenderPipeline>                             blitPipeline = nullptr;

    std::shared_ptr<RenderGraph> applyConfiguration();
    void                  compileGraph();
    void                  initBlit();
    void                  recordPresentation(
        rhi::ICommandEncoder& commands, rhi::ITexture& backBuffer, const rhi::Viewport& viewport,
        const std::function<void(rhi::IRenderPassEncoder&, uint32_t)>& recordUi, uint32_t frameIndex
    );
    void                  initSurface();
    void                  waitForFrame(uint64_t value);
};
