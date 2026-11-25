#include "render/Renderer.h"
#include "render/RenderContext.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "render/RhiUtils.h"
#include "render/graph/RenderPassStandardFlags.h"
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <slang-rhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/vec2.hpp>
#include <limits>
#include <utility>

namespace
{
constexpr std::array kRequiredDeviceFeatures{
    rhi::Feature::Surface, rhi::Feature::Rasterization, rhi::Feature::ParameterBlock, rhi::Feature::RayTracing
};

rhi::Viewport clampViewport(rhi::Viewport viewport, const rhi::SurfaceConfig& surface)
{
    const float width  = float(surface.width);
    const float height = float(surface.height);

    viewport.originX = std::floor(std::clamp(viewport.originX, 0.0f, width - 1.0f));
    viewport.originY = std::floor(std::clamp(viewport.originY, 0.0f, height - 1.0f));
    viewport.extentX = std::floor(std::clamp(viewport.extentX, 1.0f, width - viewport.originX));
    viewport.extentY = std::floor(std::clamp(viewport.extentY, 1.0f, height - viewport.originY));

    return viewport;
}

}  // namespace

Renderer::Renderer()  = default;
Renderer::~Renderer() = default;

void Renderer::init()
{
    rhi::DeviceDesc desc;
    desc.deviceType                    = rhi::DeviceType::Vulkan;
    desc.requiredFeatures              = kRequiredDeviceFeatures.data();
    desc.requiredFeatureCount          = static_cast<uint32_t>(kRequiredDeviceFeatures.size());
    desc.slang.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    desc.persistentShaderCache         = ShaderManager::persistentCache();
    desc.enableValidation              = true;
    rhiCheck(rhi::getRHI()->createDevice(desc, device.writeRef()));
    rhiCheck(device->getQueue(rhi::QueueType::Graphics, queue.writeRef()));
    rhiCheck(device->createFence({}, frameFence.writeRef()));
    shaders.init(device);
    initSurface();

    const auto& assets = *context().assetManager;
    resources.init(*device, assets);
    gpuScene.init(*device, assets, resources, shaders);
    waitIdle();
    applyConfiguration();
}

void Renderer::waitIdle() { rhiCheck(queue->waitOnHost()); }

void Renderer::waitForFrame(uint64_t value)
{
    auto* fence = frameFence.get();
    rhiCheck(device->waitForFences(1, &fence, &value, true, rhi::kTimeoutInfinite));
}

void Renderer::shutdown() noexcept
{

    waitIdle();

    mpRenderGraph = {};
    blitPipeline  = nullptr;
    gpuScene      = {};
    resources.shutdown();
    surface.setNull();
    shaders.shutdown();
    frameFence.setNull();
    queue.setNull();
    device.setNull();
    lastSubmittedFrame    = 0;
    activeFramesInFlight  = kMaxFramesInFlight;
    appliedGraphSettings = {};
    appliedPasses = {};
}

void Renderer::initBlit()
{
    rhi::ColorTargetDesc color;
    color.format = surface->getConfig()->format;
    rhi::RenderPipelineDesc desc;
    desc.program                       = shaders.getProgram("render/impl/postprocess/blit.slang", {"vertMain", "fragMain"});
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(device->createRenderPipeline(desc, blitPipeline.writeRef()));
}

void Renderer::recordPresentation(
    rhi::ICommandEncoder& commands, rhi::ITexture& backBuffer, const rhi::Viewport& viewport,
    const std::function<void(rhi::IRenderPassEncoder&, uint32_t)>& recordUi, uint32_t frameIndex
)
{
    const auto source = mpRenderGraph->getOutputTexture(0);
    const auto target = backBuffer.getDefaultView();
    CHECK(target);
    rhi::RenderPassColorAttachment color;
    color.view          = target;
    color.loadOp        = rhi::LoadOp::Clear;
    color.clearValue[3] = 1.0f;
    auto* pass          = commands.beginRenderPass({&color, 1});
    CHECK(pass);

    auto* object = pass->bindPipeline(blitPipeline);
    CHECK(object);

    const rhi::ShaderCursor root(object);
    rhiCheck(root["sourceImage"].setBinding(source));
    rhiCheck(root["pointSampler"].setBinding(resources.sampler(BuiltinAssets::Sampler::nearestClamp)));
    pass->setRenderState(viewportState(viewport));
    pass->draw({3});

    if (recordUi)
        recordUi(*pass, frameIndex);
    pass->end();
}

std::shared_ptr<RenderGraph> Renderer::applyConfiguration()
{
    const auto& settings = context().config.renderer;
    std::shared_ptr<RenderGraph> previousGraph;
    if (!mpRenderGraph || settings.parameters != appliedGraphSettings)
    {
        auto candidate = buildMinimalPathTracerGraph(device, settings.parameters);
        if (!mpRenderGraph || !mpRenderGraph->hasSameTopology(*candidate))
        {
            candidate->setScene(&gpuScene);
            // Keep the old graph alive until compileGraph() waits for the queue.
            previousGraph = std::exchange(mpRenderGraph, std::move(candidate));
        }
        appliedGraphSettings = settings.parameters;
    }

    if (previousGraph || settings.passes != appliedPasses)
    {
        mpRenderGraph->applyPassSettings(settings.passes);
        appliedPasses = settings.passes;
    }

    return previousGraph;
}

void Renderer::compileGraph()
{
    if (!mpRenderGraph->needsCompilation())
        return;
    waitIdle();

    RenderContext compileContext{.shaderManager = &shaders, .resources = &resources};
    mpRenderGraph->compile(&compileContext);
    activeFramesInFlight = mpRenderGraph->getMaxFramesInFlight();

    const auto color = mpRenderGraph->getOutputTexture(0);
    CHECK(
        color && color->getDesc().type == rhi::TextureType::Texture2D &&
            rhi::is_set(color->getDesc().usage, rhi::TextureUsage::ShaderResource),
        "first output '{}' of graph '{}' must be a shader-readable color texture",
        mpRenderGraph->getOutputName(0),
        mpRenderGraph->getName()
    );
}

void Renderer::initSurface()
{
    const auto& window = *context().window;
    rhiCheck(device->createSurface(
        rhi::WindowHandle::fromHwnd(glfwGetWin32Window(window.nativeHandle())),
        surface.writeRef()
    ));

    const auto [width, height] = window.framebufferSize();
    rhi::SurfaceConfig config;
    config.width             = uint32_t(width);
    config.height            = uint32_t(height);
    config.desiredImageCount = kMaxFramesInFlight + 1;
    config.format            = surface->getInfo().preferredFormat;
    config.usage             = rhi::TextureUsage::RenderTarget;
    rhiCheck(surface->configure(config));
    if (!blitPipeline || blitPipeline->getDesc().targets[0].format != config.format)
        initBlit();
}
void Renderer::resizeSurface()
{
    waitIdle();
    surface.setNull();
    initSurface();
}

RenderFrameResult Renderer::render(
    const Scene& scene, uint64_t sceneGeneration, const rhi::Viewport& displayViewport,
    const std::function<void(rhi::IRenderPassEncoder&, uint32_t)>& recordUi, float deltaSeconds
)
{
    const auto&      surfaceConfig = *surface->getConfig();
    const auto       viewport      = clampViewport(displayViewport, surfaceConfig);
    const glm::uvec2 renderExtent{uint32_t(viewport.extentX), uint32_t(viewport.extentY)};

    {
        const auto previousGraph = applyConfiguration();
        mpRenderGraph->onResize(renderExtent, surfaceConfig.format);
        compileGraph();
    }

    const uint64_t submissionValue = lastSubmittedFrame + 1;
    if (submissionValue > activeFramesInFlight)
        waitForFrame(submissionValue - activeFramesInFlight);

    rhi::ComPtr<rhi::ICommandEncoder> commands;
    rhiCheck(queue->createCommandEncoder(commands.writeRef()));

    const auto updates = gpuScene.update(scene, sceneGeneration, renderExtent, *commands);

    RenderContext frame{
        .commands      = commands.get(),
        .shaderManager = &shaders,
        .resources     = &resources,
        .frameIndex    = static_cast<uint32_t>(lastSubmittedFrame % activeFramesInFlight),
        .deltaSeconds  = deltaSeconds,
    };
    mpRenderGraph->onSceneUpdates(&frame, updates);
    if (mpRenderGraph->needsCompilation())
    {
        compileGraph();
        frame.frameIndex = static_cast<uint32_t>(lastSubmittedFrame % activeFramesInFlight);
    }

    rhi::ComPtr<rhi::ITexture> backBuffer;
    const auto                 acquired      = surface->acquireNextImage(backBuffer.writeRef());
    const bool                 imageAcquired = SLANG_SUCCEEDED(acquired);

    if (imageAcquired)
    {
        CHECK(backBuffer);
        mpRenderGraph->getPassesDictionary()[kRenderPassRefreshFlags] = std::to_underlying(RenderPassRefreshFlags::None);
        mpRenderGraph->execute(&frame);
        recordPresentation(*commands, *backBuffer, viewport, recordUi, frame.frameIndex);
    }
    else
    {
        const auto [width, height] = context().window->framebufferSize();
        const auto* config         = surface->getConfig();
        if (uint32_t(width) == config->width && uint32_t(height) == config->height)
            rhiCheck(acquired);
    }

    // Submit recorded scene updates even when the swapchain needs rebuilding.
    rhi::ComPtr<rhi::ICommandBuffer> commandBuffer;
    rhiCheck(commands->finish(commandBuffer.writeRef()));
    auto* buffer = commandBuffer.get();
    auto* fence  = frameFence.get();

    rhiCheck(queue->submit(
        {.commandBuffers    = &buffer,
         .commandBufferCount = 1,
         .signalFences       = &fence,
         .signalFenceValues  = &submissionValue,
         .signalFenceCount   = 1}
    ));
    lastSubmittedFrame = submissionValue;

    if (imageAcquired)
        rhiCheck(surface->present());

    return imageAcquired ? RenderFrameResult::Presented : RenderFrameResult::SwapchainNeedsRebuild;
}
