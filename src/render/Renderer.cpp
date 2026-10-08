#include "render/Renderer.h"
#include "render/RenderContext.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "scene/SceneManager.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/Editor.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/feature/DepthPrepass.h"
#include "render/feature/shadow/PointLightShadowPass.h"
#include "render/feature/shadow/RectAreaLightShadowPass.h"
#include "render/feature/shadow/DirectionalShadowPass.h"
#include "render/feature/ScenePass.h"
#include "render/feature/postprocess/ToneMappingPass.h"
#include "render/feature/editor/EditorPickingCopyPass.h"
#include "render/feature/editor/EditorPickingPass.h"
#include "render/feature/editor/EditorUiPass.h"
#include "render/feature/editor/LightMarkerPass.h"
#include "render/raytracing/RayTracingBlitPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/resource/Buffer.h"
#include "editor/EditorCamera.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <tuple>
#include <utility>

struct Renderer::Passes
{
    std::tuple<
        PointLightShadowPass,
        RectAreaLightShadowPass,
        DirectionalShadowPass,
        DepthPrepass,
        ScenePass,
        LightMarkerPass,
        ToneMappingPass,
        RayTracingPass,
        RayTracingBlitPass,
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

Renderer::~Renderer() { DCHECK(!inited); }

void Renderer::init()
{
    DCHECK(!inited);

    auto& ctx = renderContext();
    DCHECK(!ctx.shaderManager && !ctx.descriptors && !ctx.pipelineManager && !ctx.resources);
    ctx = {
        .shaderManager   = &shaders,
        .descriptors     = &descriptors,
        .pipelineManager = &pipelines,
        .resources       = &resources,
    };

    initVulkan();
    DEBUG_EXEC(inited = true);
}

EditorFrameResult Renderer::render(const EditorCamera& camera, const EditorFrameInput& editor)
{
    DCHECK(inited);
    return drawFrame(camera, editor);
}

void Renderer::loadScene()
{
    DCHECK(inited);

    waitIdle();
    DCHECK(context().sceneManager);
    const auto& targetScene = context().sceneManager->scene();
    renderSystem.load(targetScene, *renderContext().resources);
    iblParameters              = {};
    const auto& environmentMap = context().config->rendererConfig().environmentMap;
    if (environmentMap)
    {
        const auto& environment = context().assetManager->get<EnvironmentMapAsset>(*environmentMap);
        const auto& prefiltered = context().assetManager->get<TextureAsset>(environment.prefilteredSpecular.textureID);
        iblParameters.x         = static_cast<float>(prefiltered.mipLevels - 1);
    }
    setupRenderGraph();
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
    auto& ctx = renderContext();

    if (*vulkan.deviceHandle())
    {
        (void)vulkan.deviceHandle().waitIdle();
    }

    renderSystem.reset();
    graph.reset();
    passes.reset();
    pickingReadback = {};
    ctx.resources->shutdown();
    ctx.pipelineManager->shutdown();
    for (FrameContext& frame : frames)
    {
        frame.reset();
    }
    ctx.descriptors->shutdown();
    ctx.shaderManager->shutdown();
    ctx = {};
    swapchain.reset();
    vulkan.reset();
    frameIndex    = 0;
    iblParameters = {};
    DEBUG_EXEC(inited = false);
}

VulkanContext& Renderer::vulkanContext() { return vulkan; }

Swapchain& Renderer::swapchainHandle() { return swapchain; }

const RenderData& Renderer::renderData() const { return renderSystem.renderData(); }

FrameContext& Renderer::currentFrame() { return frames[frameIndex]; }

void Renderer::initVulkan()
{
    DCHECK(context().window);
    DCHECK(context().assetManager);
    DCHECK(context().config);

    Window& window = *context().window;
    vulkan.init(window);
    swapchain.init(vulkan, window);
    auto& ctx = renderContext();
    ctx.shaderManager->init(vulkan.deviceHandle());
    ctx.descriptors->init(vulkan.deviceHandle());
    ctx.pipelineManager->init(vulkan.deviceHandle(), *ctx.descriptors, *ctx.shaderManager);
    for (FrameContext& frame : frames)
    {
        frame.init(vulkan, *ctx.descriptors);
    }
    ctx.resources->init(vulkan, *ctx.descriptors, *ctx.shaderManager);

    graph.init();
    passes = std::make_shared<Passes>();
}

void Renderer::setupRenderGraph()
{
    DCHECK(passes);
    waitIdle();
    graph.releaseResources();

    const auto               color = graph.importSwapchain();
    RenderGraph::ImageHandle sceneColor;
    RenderGraph::ImageHandle sceneDepth;
    const auto&              config     = context().config->rendererConfig();
    const bool               rayTracing = config.renderPath == RenderPath::RayTracing;
    if (rayTracing)
    {
        const auto [rayImage] = setupPass<RayTracingPass>();
        sceneColor            = setupPass<RayTracingBlitPass>(rayImage, color).color;
    }
    else
    {
        const auto shadow            = setupPass<PointLightShadowPass>();
        const auto rectAreaShadow    = setupPass<RectAreaLightShadowPass>();
        const auto directionalShadow = setupPass<DirectionalShadowPass>();
        const auto depth             = setupPass<DepthPrepass>();
        const auto scene             = setupPass<ScenePass>(depth.depth, shadow, rectAreaShadow, directionalShadow);
        const auto markers           = setupPass<LightMarkerPass>(scene.color, scene.depth);
        sceneColor = config.feature.hdr ? setupPass<ToneMappingPass>(markers.color, color).color : markers.color;
        sceneDepth = markers.depth;
    }

    const auto [pickingImage] = setupPass<EditorPickingPass>(sceneDepth);
    const auto [readback]     = setupPass<EditorPickingCopyPass>(pickingImage);
    const auto [finalColor]   = setupPass<EditorUiPass>(sceneColor);
    setupPass<EndPass>(finalColor, readback);
    pickingReadback = readback;

    graph.compile();
    if (!rayTracing)
    {
        passes->get<ScenePass>().bindResources(graph);
        if (config.feature.hdr)
            passes->get<ToneMappingPass>().bindResources(graph);
    }
}

void Renderer::recreateSwapchain()
{
    Window& window = *context().window;
    while (!window.shouldClose())
    {
        const auto [width, height] = window.framebufferSize();
        if (width > 0 && height > 0)
        {
            break;
        }
        window.waitEvents();
    }
    if (window.shouldClose())
    {
        return;
    }

    waitIdle();
    graph.releaseResources();
    swapchain.reset();
    swapchain.init(vulkan, window);
    setupRenderGraph();
    context().editor->refreshUi();
}

void Renderer::updateFrameData(const EditorCamera& camera, const EditorFrameInput& editor)
{
    renderSystem.prepare(camera.worldPosition(), *renderContext().resources);

    const glm::mat4    viewMatrix     = camera.viewMatrix();
    const glm::mat4    viewProjection = camera.projectionMatrix(editor.aspectRatio) * viewMatrix;
    const ViewUniforms view{
        .viewProjection        = viewProjection,
        .inverseViewProjection = glm::inverse(viewProjection),
        .cameraPosition        = glm::vec4{camera.worldPosition(), 1.0f},
        .view                  = viewMatrix,
    };
    const auto& lights = renderSystem.renderData().lights;
    auto        shadow = context().config->rendererConfig().feature.shadow;
    shadow.enable      = shadow.enable && context().config->rendererConfig().renderPath == RenderPath::Rasterization;
    const LightUniforms lighting{
        .lightCount         = static_cast<uint32_t>(lights.size()),
        .ambientCoefficient = context().config->rendererConfig().feature.ambientCoefficient,
        .iblParameters      = iblParameters,
        .shadowParameters   = {shadow.bias, shadow.sampleRadius, shadow.pixelRotation ? 1.0f : 0.0f, 0.0f},
    };
    currentFrame().data.update(view, lighting, lights, shadow);
}

EditorFrameResult Renderer::drawFrame(const EditorCamera& camera, const EditorFrameInput& editor)
{
    const vk::Fence         drawFence               = currentFrame().drawFenceHandle();
    const vk::Semaphore     imageAvailableSemaphore = currentFrame().imageAvailableSemaphore();
    const vk::CommandBuffer commandBuffer           = *currentFrame().commandBufferHandle();

    const auto fenceResult = vulkan.deviceHandle().waitForFences(drawFence, vk::True, UINT64_MAX);
    CHECK(fenceResult == vk::Result::eSuccess, "failed to wait for fence!");

    uint32_t   imageIndex    = 0;
    const auto acquireResult = static_cast<vk::Result>(swapchain.handle().getDispatcher()->vkAcquireNextImageKHR(
        static_cast<VkDevice>(*vulkan.deviceHandle()),
        static_cast<VkSwapchainKHR>(*swapchain.handle()),
        UINT64_MAX,
        static_cast<VkSemaphore>(imageAvailableSemaphore),
        VK_NULL_HANDLE,
        &imageIndex
    ));
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapchain();
        return {};
    }
    CHECK(
        acquireResult == vk::Result::eSuccess || acquireResult == vk::Result::eSuboptimalKHR,
        "vkAcquireNextImageKHR failed: {}",
        vk::to_string(acquireResult)
    );
    const vk::Semaphore renderFinishedSemaphore = swapchain.renderFinishedSemaphore(imageIndex);
    vkCheck(vulkan.deviceHandle().resetFences(drawFence));

    const bool resolvePickAfterSubmit = editor.requestPick;
    updateFrameData(camera, editor);
    graph.execute(editor, frameIndex, imageIndex);

    vk::PipelineStageFlags waitDestinationStageMask(
        context().config->rendererConfig().renderPath == RenderPath::RayTracing ? vk::PipelineStageFlagBits::eTransfer
                                        : vk::PipelineStageFlagBits::eColorAttachmentOutput
    );
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &imageAvailableSemaphore,
        .pWaitDstStageMask    = &waitDestinationStageMask,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &renderFinishedSemaphore
    };
    vkCheck(vulkan.queueHandle().submit(submitInfo, drawFence));

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &renderFinishedSemaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &*swapchain.handle(),
        .pImageIndices      = &imageIndex,
    };
    const auto presentResult = static_cast<vk::Result>(vulkan.queueHandle().getDispatcher()->vkQueuePresentKHR(
        static_cast<VkQueue>(*vulkan.queueHandle()),
        reinterpret_cast<const VkPresentInfoKHR*>(&presentInfoKHR)
    ));
    CHECK(
        presentResult == vk::Result::eSuccess || presentResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eErrorOutOfDateKHR,
        "vkQueuePresentKHR failed: {}",
        vk::to_string(presentResult)
    );

    EditorFrameResult frameResult;
    if (resolvePickAfterSubmit)
    {
        const vk::Result pickFenceResult = vulkan.deviceHandle().waitForFences(drawFence, vk::True, UINT64_MAX);
        CHECK(pickFenceResult == vk::Result::eSuccess, "failed to wait for object picking readback");

        graph.getBuffer(pickingReadback, frameIndex).download(
            &frameResult.pickedSelectionId, sizeof(frameResult.pickedSelectionId)
        );
        frameResult.hasPickResult = true;
    }
    if (acquireResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapchain();
    }
    frameIndex = (frameIndex + 1) % maxFramesInFlight;
    return frameResult;
}
