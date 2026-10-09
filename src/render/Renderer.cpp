#include "render/Renderer.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/feature/DepthPrepass.h"
#include "render/feature/shadow/ShadowPass.h"
#include "render/feature/ScenePass.h"
#include "render/feature/SkyboxPass.h"
#include "render/feature/TransparentPass.h"
#include "render/feature/postprocess/ToneMappingPass.h"
#include "render/feature/editor/EditorPickingCopyPass.h"
#include "render/feature/editor/EditorPickingPass.h"
#include "render/feature/editor/EditorUiPass.h"
#include "render/feature/editor/LightMarkerPass.h"
#include "render/feature/editor/EditorDepthPass.h"
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
        SkyboxPass,
        TransparentPass,
        LightMarkerPass,
        ToneMappingPass,
        RayTracingPass,
        RayTracingBlitPass,
        EditorDepthPass,
        EditorPickingPass,
        EditorPickingCopyPass,
        EditorUiPass,
        EndPass>
        data;

    template <class T>
    T& get() { return std::get<T>(data); }
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

    shaders.init(vulkan.deviceHandle());
    descriptors.init(vulkan.deviceHandle());
    pipelines.init(vulkan.deviceHandle(), descriptors, shaders);

    for (auto& frame : frames)
    {
        frame.init(vulkan, descriptors);
    }

    resources.init(vulkan, descriptors, shaders);
    graph.init(vulkan, swapchain, frames);
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
    descriptors.shutdown();
    shaders.shutdown();
    renderContext() = {};
    swapchain.reset();
    vulkan.reset();
    frameIndex    = 0;
    DEBUG_EXEC(inited = false);
}

void Renderer::rebuildGraph()
{
    DCHECK(inited);
    waitIdle();
    graph.releaseResources();
    setupRenderGraph();
}

void Renderer::setupRenderGraph()
{
    DCHECK(passes);

    const auto               color = graph.importSwapchain();
    RenderGraph::ImageHandle sceneColor;
    RenderGraph::ImageHandle sceneDepth;
    const auto&              config     = context().config->rendererConfig();
    const bool               rayTracing = config.renderPath == RenderPath::RayTracing;
    if (rayTracing)
    {
        const auto [rayImage] = setupPass<RayTracingPass>();
        sceneColor = config.feature.hdr ? setupPass<ToneMappingPass>(rayImage, color).color
                                        : setupPass<RayTracingBlitPass>(rayImage, color).color;
        sceneDepth            = setupPass<EditorDepthPass>().depth;
    }
    else
    {
        ShadowPass::Output shadow;
        if (config.feature.shadow.enable)
            shadow = setupPass<ShadowPass>();

        const auto depth = setupPass<DepthPrepass>();
        auto scene = setupPass<ScenePass>(depth.depth, shadow);

        if (config.feature.ibl && config.environmentMap)
            scene.color = setupPass<SkyboxPass>(scene.color, scene.depth).color;

        const auto transparent = setupPass<TransparentPass>(scene.color, scene.depth, scene.shadow);
        sceneColor = config.feature.hdr ? setupPass<ToneMappingPass>(transparent.color, color).color : transparent.color;
        sceneDepth = scene.depth;
    }

    const auto markers = setupPass<LightMarkerPass>(sceneColor, sceneDepth);
    const auto [pickingImage] = setupPass<EditorPickingPass>(markers.depth);
    const auto [readback]     = setupPass<EditorPickingCopyPass>(pickingImage);
    const auto [finalColor]   = setupPass<EditorUiPass>(markers.color);
    setupPass<EndPass>(finalColor, readback);
    pickingReadback = readback;

    graph.compile();
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

RenderFrameResult Renderer::render(const RenderView& view, const RenderData& renderData, const EditorFrameInput& editor)
{
    DCHECK(inited);
    auto& frame = currentFrame();
    frame.wait();

    const auto [acquireResult, imageIndex] = swapchain.acquire(frame.imageAvailableSemaphore());
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        return {.swapchainNeedsRebuild = true};

    const auto renderPath = context().config->rendererConfig().renderPath;
    if (renderPath == RenderPath::Rasterization)
        passes->get<ScenePass>().updateFrame(frame.data, view.uniforms, renderData);
    else
        frame.data.updateView(view.uniforms);
    graph.execute(view, renderData, editor, frameIndex, imageIndex);

    const bool directBlit = renderPath == RenderPath::RayTracing && !context().config->rendererConfig().feature.hdr;
    const vk::PipelineStageFlags waitStages = directBlit
        ? vk::PipelineStageFlagBits::eTransfer
        : vk::PipelineStageFlagBits::eColorAttachmentOutput;
    frame.submit(swapchain.renderFinishedSemaphore(imageIndex), waitStages);
    const auto presentResult = swapchain.present(imageIndex);

    RenderFrameResult frameResult{
        .swapchainNeedsRebuild = acquireResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR,
    };
    if (editor.requestPick)
    {
        frame.wait();

        graph.getBuffer(pickingReadback, frameIndex).download(
            &frameResult.pickedSelectionId, sizeof(frameResult.pickedSelectionId)
        );
        frameResult.hasPickResult = true;
    }
    frameIndex = (frameIndex + 1) % maxFramesInFlight;
    return frameResult;
}
