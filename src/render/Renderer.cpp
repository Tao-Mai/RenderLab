#include "render/Renderer.h"
#include "render/RenderContext.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "render/device/RhiCheck.h"
#include "render/graph/RenderGraphScript.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <slang-rhi.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace
{
constexpr std::array kRequiredDeviceFeatures{
    rhi::Feature::Surface, rhi::Feature::Rasterization, rhi::Feature::ParameterBlock
};

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
    desc.enableValidation              = true;
    rhiCheck(rhi::getRHI()->createDevice(desc, device.writeRef()));
    rhiCheck(device->getQueue(rhi::QueueType::Graphics, queue.writeRef()));
    rhiCheck(device->createFence({}, frameFence.writeRef()));
    shaders.init(device);
    initSurface();

    auto& assets = *context().assetManager;
    auto& assetData = *context().assetDataManager;
    resources.init(*device, assets, assetData);
    gpuScene.init(*device, assets, assetData, resources, shaders);
    if (!pendingGraphName)
        pendingGraphName = context().config->renderGraphName();
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
    waitForPathTracingPipelineBuilds();
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
    appliedParameters     = {};
    pendingGraphName.reset();
    optionsDirty = false;
    parameterDefaults = Properties::json::object();
    parameters        = Properties::json::object();
}

void Renderer::switchGraph(std::string_view name)
{
    auto& config = *context().config;
    config.setRenderGraph(name);
    pendingGraphName = config.renderGraphName();
}

void Renderer::configureGraph(const std::string& graphName, const nlohmann::json& parameterOverrides)
{
    auto loaded = loadRenderGraph(*device, context().config->renderGraphScript(graphName), graphName, parameterOverrides);
    parameterDefaults = std::move(loaded.defaults);
    parameters = std::move(loaded.parameters);

    auto& candidate = *loaded.graph;
    if (mpRenderGraph && mpRenderGraph->hasSameTopology(candidate))
    {
        mpRenderGraph->applyScriptedProperties(candidate);
        return;
    }

    const auto& surfaceConfig = *surface->getConfig();
    candidate.onResize({surfaceConfig.width, surfaceConfig.height}, surfaceConfig.format);
    candidate.setScene(&gpuScene);
    mpRenderGraph = std::move(loaded.graph);
}

void Renderer::initBlit()
{
    rhi::ColorTargetDesc color;
    color.format = surface->getConfig()->format;
    rhi::RenderPipelineDesc desc;
    desc.program                       = shaders.getProgram("shaders/raster/blit.slang", {"vertMain", "fragMain"});
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(device->createRenderPipeline(desc, blitPipeline.writeRef()));
}

void Renderer::recordPresentation(
    rhi::ICommandEncoder& commands, rhi::ITexture& backBuffer, const EditorFrameInput& editor, uint32_t frameIndex
)
{
    const auto source = mpRenderGraph->getOutputTexture(0);
    const auto target = backBuffer.getDefaultView();
    CHECK(target);
    rhi::RenderPassColorAttachment color;
    color.view   = target;
    color.loadOp = rhi::LoadOp::DontCare;
    auto* pass   = commands.beginRenderPass({&color, 1});
    CHECK(pass);

    auto* object = pass->bindPipeline(blitPipeline);
    CHECK(object);

    const rhi::ShaderCursor root(object);
    rhiCheck(root["sourceImage"].setBinding(source));
    rhiCheck(root["pointSampler"].setBinding(resources.sampler(BuiltinAssets::Sampler::nearestClamp)));
    const auto& size = backBuffer.getDesc().size;
    pass->setRenderState(ViewportRect{0, 0, float(size.width), float(size.height)}.renderState());
    pass->draw({3});

    if (editor.recordUi)
        editor.recordUi(*pass, frameIndex);
    pass->end();
}

void Renderer::addOption(std::string_view name, nlohmann::json value)
{
    context().config->getSettings().addOption(name, value);
    optionsDirty = true;
}

void Renderer::addOptions(const nlohmann::json& options)
{
    context().config->getSettings().addOptions(options);
    optionsDirty = true;
}

void Renderer::clearOptions()
{
    context().config->getSettings().clearOptions();
    optionsDirty = true;
}

void Renderer::applyConfiguration()
{
    ref<RenderGraph> previousGraph;
    if (pendingGraphName || optionsDirty)
    {
        const auto& graphName = pendingGraphName ? *pendingGraphName : mpRenderGraph->getName();
        auto        options   = context().config->getOptions(graphName);
        auto& parameterOverrides = options.at("parameters");
        if (pendingGraphName || parameterOverrides != appliedParameters)
        {
            // Keep a replaced graph alive until its queued work finishes.
            previousGraph = mpRenderGraph;
            configureGraph(graphName, parameterOverrides);
            appliedParameters = std::move(parameterOverrides);
        }
        mpRenderGraph->applyPassOverrides(options.at("passes"));

        pendingGraphName.reset();
        optionsDirty = false;
    }

    if (!mpRenderGraph->needsCompilation())
        return;
    waitIdle();
    previousGraph.reset();

    RenderContext compileContext{.shaderManager = &shaders, .resources = &resources};
    std::string log;
    CHECK(
        mpRenderGraph->compile(&compileContext, log),
        "failed to compile render graph '{}': {}",
        mpRenderGraph->getName(),
        log
    );
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
    auto& window = *context().window;
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
    mpRenderGraph->onResize({surface->getConfig()->width, surface->getConfig()->height}, surface->getConfig()->format);
    applyConfiguration();
}

RenderFrameResult Renderer::render(const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds)
{
    // 获取最新渲染配置
    applyConfiguration();

    const uint64_t submissionValue = lastSubmittedFrame + 1;
    if (submissionValue > activeFramesInFlight)
        waitForFrame(submissionValue - activeFramesInFlight);

    rhi::ComPtr<rhi::ICommandEncoder> commands;
    rhiCheck(queue->createCommandEncoder(commands.writeRef()));

    const auto updates = gpuScene.update(scene, *commands);

    RenderContext frame{
        .commands      = commands.get(),
        .shaderManager = &shaders,
        .resources     = &resources,
        .editor        = &editor,
        .frameIndex    = static_cast<uint32_t>(lastSubmittedFrame % activeFramesInFlight),
        .deltaSeconds  = deltaSeconds,
    };
    mpRenderGraph->onSceneUpdates(&frame, updates);
    if (mpRenderGraph->needsCompilation())
    {
        applyConfiguration();
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
        recordPresentation(*commands, *backBuffer, editor, frame.frameIndex);
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
    {
        rhiCheck(surface->present());
        if (!frame.onGpuComplete.empty())
        {
            waitForFrame(submissionValue);
            frame.notifyGpuComplete();
        }
    }

    return imageAcquired ? RenderFrameResult::Presented : RenderFrameResult::SwapchainNeedsRebuild;
}
