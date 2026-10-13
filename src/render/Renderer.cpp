#include "render/Renderer.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "scene/system/SceneRenderData.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/profiling/Profiler.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "asset/AssetManager.h"
#include "render/offline/OfflinePathTracingPass.h"
#include "render/device/VkCheck.h"
#include "render/feature/DepthPrepass.h"
#include "render/feature/EndPass.h"
#include "render/feature/ScenePass.h"
#include "render/feature/SkyboxPass.h"
#include "render/feature/TransparentPass.h"
#include "render/feature/deferred/DeferredLightingPass.h"
#include "render/feature/deferred/GBufferPass.h"
#include "render/feature/editor/EditorDepthPass.h"
#include "render/feature/editor/EditorPickingCopyPass.h"
#include "render/feature/editor/EditorPickingPass.h"
#include "render/feature/editor/EditorUiPass.h"
#include "render/feature/editor/LightMarkerPass.h"
#include "render/feature/postprocess/ToneMappingPass.h"
#include "render/feature/shadow/ShadowPass.h"
#include "render/raytracing/RayTracingBlitPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/resource/Buffer.h"

#include <tuple>
#include <utility>

struct Renderer::Passes
{
    std::tuple<
        ShadowPass,
        DepthPrepass,
        ScenePass,
        GBufferPass,
        DeferredLightingPass,
        SkyboxPass,
        TransparentPass,
        LightMarkerPass,
        ToneMappingPass,
        RayTracingPass,
        OfflinePathTracingPass,
        RayTracingBlitPass,
        EditorDepthPass,
        EditorPickingPass,
        EditorPickingCopyPass,
        EditorUiPass,
        EndPass>
        data;

    template <class T>
    T& get()
    {
        return std::get<T>(data);
    }
};

template <class T, class... Args>
auto Renderer::setupPass(Args&&... args)
{
    return graph.addPass(passes->get<T>(), std::forward<Args>(args)...);
}

Renderer::Renderer() = default;
Renderer::~Renderer() { DCHECK(!inited); }

void Renderer::init()
{
    DCHECK(!inited);

    auto& ctx = renderContext();
    DCHECK(!ctx.shaderManager && !ctx.descriptors && !ctx.pipelineManager && !ctx.resources);
    DCHECK(context().window);

    auto& window = *context().window;
    vulkan.init(window);
    swapchain.init(vulkan, window);

    shaders.init(vulkan.deviceHandle(), *context().assetManager);
    descriptors.init(vulkan.deviceHandle());
    pipelines.init(vulkan.deviceHandle(), descriptors, shaders);

    for (auto& frame : frames)
    {
        frame.init(vulkan);
    }

    for (auto& data : frameStorage)
        data.init(vulkan, descriptors);

    resources.init(vulkan, descriptors, shaders, *context().assetManager, *context().assetDataManager);
    graph.init(vulkan, swapchain, frames, frameStorage);
    passes = std::make_unique<Passes>();

    ctx = {
        .shaderManager   = &shaders,
        .descriptors     = &descriptors,
        .pipelineManager = &pipelines,
        .resources       = &resources,
    };
    DEBUG_EXEC(inited = true);
}

void Renderer::waitIdle()
{
    CpuProfileScope timing{context().profiler, "Device idle wait"};
    if (*vulkan.deviceHandle())
    {
        vkCheck(vulkan.deviceHandle().waitIdle());
    }
}

void Renderer::shutdown() noexcept
{
    DCHECK(inited);
    waitIdle();

    graph.reset();
    passes.reset();
    pickingReadback = {};
    resources.shutdown();
    pipelines.shutdown();
    for (FrameContext& frame : frames)
    {
        frame.reset();
    }
    for (auto& data : frameStorage)
        data.reset();
    descriptors.shutdown();
    shaders.shutdown();
    renderContext() = {};
    swapchain.reset();
    vulkan.reset();
    frameIndex = 0;
    activeFramesInFlight = maxFramesInFlight;
    environmentMap.reset();
    appliedEnvironmentMap.reset();
    appliedConfig.reset();
    DEBUG_EXEC(inited = false);
}

void Renderer::rebuildGraph()
{
    CpuProfileScope timing{context().profiler, "Graph rebuild"};
    DCHECK(inited);
    waitIdle();
    graph.releaseResources();
    setupRenderGraph();
}

void Renderer::setupRenderGraph()
{
    if (context().profiler) context().profiler->clearGpu();
    for (auto& frame : frames) frame.gpuProfiler().discard();
    DCHECK(passes);
    activeSceneBindings = nullptr;
    const auto& config  = context().config->rendererConfig();
    activeFramesInFlight = config.renderPath == RenderPath::OfflinePathTracing ? 1u : maxFramesInFlight;
    frameIndex = 0;
    if (!appliedConfig || appliedConfig->renderPath != config.renderPath ||
        appliedConfig->feature.offline.maxRecursionDepth != config.feature.offline.maxRecursionDepth)
        passes->get<OfflinePathTracingPass>().resetAccumulation();

    SceneOutput scene;
    switch (config.renderPath)
    {
    case RenderPath::OfflinePathTracing: scene = setupOffline(); break;
    case RenderPath::RayTracing: scene = setupRayTracing(); break;
    case RenderPath::Rasterization:
    case RenderPath::Deferred: scene = setupRasterization(); break;
    }
    scene.color         = setupPostProcess(scene.color);
    setupEditor(scene);
    graph.compile();
    appliedConfig = config;
    appliedEnvironmentMap = environmentMap;
}

Renderer::SceneOutput Renderer::setupRayTracing()
{
    return {setupPass<RayTracingPass>().rayImage, setupPass<EditorDepthPass>().depth};
}

Renderer::SceneOutput Renderer::setupOffline()
{
    return {setupPass<OfflinePathTracingPass>().rayImage, setupPass<EditorDepthPass>().depth};
}

uint32_t Renderer::offlineSampleCount() const
{
    return passes ? passes->get<OfflinePathTracingPass>().sampleCount() : 0;
}

Renderer::SceneOutput Renderer::setupRasterization()
{
    const auto&        config = context().config->rendererConfig();
    ShadowPass::Output shadow;
    if (config.feature.shadow.enable)
        shadow = setupPass<ShadowPass>();
    const auto depth = setupPass<DepthPrepass>().depth;

    SceneOutput output;
    if (config.renderPath == RenderPath::Deferred)
    {
        passes->get<DeferredLightingPass>().sceneBindings().setEnvironmentMap(environmentMap);
        const auto gBuffer  = setupPass<GBufferPass>(depth);
        const auto lighting = setupPass<DeferredLightingPass>(gBuffer, shadow);
        output              = {lighting.color, lighting.depth};
        shadow              = lighting.shadow;
        activeSceneBindings = &passes->get<DeferredLightingPass>().sceneBindings();
    }
    else
    {
        passes->get<ScenePass>().sceneBindings().setEnvironmentMap(environmentMap);
        const auto scene    = setupPass<ScenePass>(depth, shadow);
        output              = {scene.color, scene.depth};
        shadow              = scene.shadow;
        activeSceneBindings = &passes->get<ScenePass>().sceneBindings();
    }
    if (config.feature.ibl && environmentMap)
    {
        passes->get<SkyboxPass>().setEnvironmentMap(*environmentMap);
        output.color = setupPass<SkyboxPass>(output.color, output.depth).color;
    }
    output.color = setupPass<TransparentPass>(output.color, output.depth, shadow).color;
    return output;
}

RenderGraph::ImageHandle Renderer::setupPostProcess(RenderGraph::ImageHandle color)
{
    const auto& config = context().config->rendererConfig();
    if (config.feature.hdr)
        return setupPass<ToneMappingPass>(color, graph.importSwapchain()).color;
    if (config.renderPath == RenderPath::RayTracing || config.renderPath == RenderPath::OfflinePathTracing)
        return setupPass<RayTracingBlitPass>(color, graph.importSwapchain()).color;
    return color;
}

void Renderer::setupEditor(SceneOutput scene)
{
    const auto markers        = setupPass<LightMarkerPass>(scene.color, scene.depth);
    const auto [pickingImage] = setupPass<EditorPickingPass>(markers.depth);
    const auto [readback]     = setupPass<EditorPickingCopyPass>(pickingImage);
    const auto [finalColor]   = setupPass<EditorUiPass>(markers.color);
    setupPass<EndPass>(finalColor, readback);
    pickingReadback = readback;
}

void Renderer::applyConfiguration(const SceneRenderData& scene)
{
    environmentMap = scene.environmentMap;
    const auto& config   = context().config->rendererConfig();
    const auto  graphKey = [](const RendererConfig& value)
    {
        const auto& shadow = value.feature.shadow;
        return std::tuple{
            value.renderPath,
            value.feature.ibl,
            value.feature.hdr,
            value.feature.rayTracing.maxRecursionDepth,
            value.feature.offline.maxRecursionDepth,
            shadow.enable,
            shadow.pointShadowMapSize,
            shadow.rectAreaShadowMapSize,
            shadow.spotShadowMapSize,
            shadow.cascade.shadowMapSize,
            shadow.cascade.count
        };
    };
    if (!appliedConfig || graphKey(*appliedConfig) != graphKey(config) ||
        appliedEnvironmentMap.has_value() != environmentMap.has_value())
    {
        rebuildGraph();
        return;
    }
    if (appliedEnvironmentMap != environmentMap || appliedConfig->brdfLut != config.brdfLut ||
        appliedConfig->feature.shadow.filter != config.feature.shadow.filter)
    {
        waitIdle();
        if (activeSceneBindings)
        {
            activeSceneBindings->setEnvironmentMap(environmentMap);
            activeSceneBindings->refreshBindings();
            activeSceneBindings->prepare(graph);
        }
        if (activeSceneBindings && config.feature.ibl && environmentMap)
        {
            passes->get<SkyboxPass>().setEnvironmentMap(*environmentMap);
            passes->get<SkyboxPass>().refreshBindings(graph);
        }
    }
    appliedConfig = config;
    appliedEnvironmentMap = environmentMap;
}

void Renderer::recreateSwapchain()
{
    DCHECK(inited);
    Window& window = *context().window;
    DEBUG_ONLY(const auto [width, height] = window.framebufferSize();)
    DCHECK(width > 0 && height > 0);
    waitIdle();
    graph.releaseResources();
    swapchain.reset();
    swapchain.init(vulkan, window);
    setupRenderGraph();
}

RenderFrameResult Renderer::render(const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor)
{
    DCHECK(inited);
    applyConfiguration(scene);
    auto& frame = currentFrame();
    {
        CpuProfileScope timing{context().profiler, "Fence wait"};
        frame.wait();
    }

    const auto [acquireResult, imageIndex] = [&]
    {
        CpuProfileScope timing{context().profiler, "Swapchain acquire"};
        return swapchain.acquire(frame.imageAvailableSemaphore());
    }();
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        return {.swapchainNeedsRebuild = true};

    const auto& renderData = [&]() -> const RenderData&
    {
        CpuProfileScope timing{context().profiler, "Render resources"};
        return resources.prepare(scene, frameIndex);
    }();
    const auto  renderPath = context().config->rendererConfig().renderPath;
    if (renderPath == RenderPath::OfflinePathTracing)
        passes->get<OfflinePathTracingPass>().updateScene(view, scene, context().assetManager->contentRevision());
    auto&       data       = frameStorage[frameIndex];
    {
        CpuProfileScope timing{context().profiler, "Frame upload"};
        if (activeSceneBindings)
            activeSceneBindings->updateFrame(data, view.uniforms, scene.lighting);
        else
            data.updateView(view.uniforms);
    }
    {
        CpuProfileScope timing{context().profiler, "Command recording"};
        graph.execute(view, renderData, editor, frameIndex, imageIndex);
    }

    const bool directBlit = (renderPath == RenderPath::RayTracing || renderPath == RenderPath::OfflinePathTracing) &&
        !context().config->rendererConfig().feature.hdr;
    const vk::PipelineStageFlags waitStages =
        directBlit ? vk::PipelineStageFlagBits::eTransfer : vk::PipelineStageFlagBits::eColorAttachmentOutput;
    {
        CpuProfileScope timing{context().profiler, "Queue submit"};
        frame.submit(swapchain.renderFinishedSemaphore(imageIndex), waitStages);
    }
    const auto presentResult = [&]
    {
        CpuProfileScope timing{context().profiler, "Present"};
        return swapchain.present(imageIndex);
    }();

    RenderFrameResult frameResult{
        .swapchainNeedsRebuild = acquireResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR,
    };
    if (editor.requestPick)
    {
        CpuProfileScope timing{context().profiler, "Picking wait/readback"};
        frame.wait();

        graph.getBuffer(pickingReadback, frameIndex)
            .download(&frameResult.pickedSelectionId, sizeof(frameResult.pickedSelectionId));
        frameResult.hasPickResult = true;
    }
    frameIndex = (frameIndex + 1) % activeFramesInFlight;
    return frameResult;
}
