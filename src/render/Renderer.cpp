#include "render/Renderer.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "scene/system/SceneRenderData.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/GlobalState.h"
#include "core/Logger.h"
#include "core/Scripting.h"
#include "core/Window.h"
#include "render/device/RhiCheck.h"
#include "render/graph/RenderGraphIR.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <slang-rhi.h>
#include "render/resources/ShaderLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace
{
constexpr std::array kRequiredDeviceFeatures{rhi::Feature::Surface, rhi::Feature::Rasterization};

Properties::json resolveParameters(const Properties::json& defaults, const nlohmann::json& overrides)
{
    auto parameters = defaults;
    for (auto& [name, value] : parameters.items())
    {
        CHECK(value.is_primitive() && !value.is_null(), "unsupported render parameter '{}'", name);
        const auto edited = overrides.find(name);
        if (edited == overrides.end())
            continue;

        if (value.is_number_float())
        {
            CHECK(
                edited->is_number() && std::isfinite(edited->get<double>()),
                "expected a finite number for '{}'",
                name
            );
            value = edited->get<double>();
        }
        else if (value.is_number_integer())
        {
            CHECK(
                edited->is_number_integer() &&
                    (!edited->is_number_unsigned() || std::in_range<int64_t>(edited->get<uint64_t>())),
                "expected a signed 64-bit integer for '{}'",
                name
            );
            value = edited->get<int64_t>();
        }
        else
        {
            CHECK(value.type() == edited->type(), "invalid type for render parameter '{}'", name);
            value = *edited;
        }
    }
    return parameters;
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
    desc.enableValidation              = true;
    rhiCheck(rhi::getRHI()->createDevice(desc, device.writeRef()));
    rhiCheck(device->getQueue(rhi::QueueType::Graphics, queue.writeRef()));
    rhiCheck(device->createFence({}, frameFence.writeRef()));
    shaders.init(device);
    initSurface();

    resources.init(*device, *context().assetManager, *context().assetDataManager);
    preparedScene.init(*device, resources);
    materialTable.init(resources);
    renderContext = {
        .device           = device.get(),
        .shaderManager    = &shaders,
        .resources        = &resources,
        .materialTable    = &materialTable,
        .pathPipelines = &pathPipelines,
        .scene            = &preparedScene,
    };

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
    renderContext = {};
    commandResources.reset();
    pathPipelines.clear();
    sceneAccelerationStructures.reset();
    blitPipeline  = nullptr;
    preparedScene = {};
    materialTable = {};
    shading  = {};
    resources.shutdown();
    surface.setNull();
    shaders.shutdown();
    frameFence.setNull();
    queue.setNull();
    device.setNull();
    lastSubmittedFrame    = 0;
    activeFramesInFlight  = kMaxFramesInFlight;
    appliedParameters     = {};
    pendingOptions.reset();
    parameterDefaults = Properties::json::object();
    parameters        = Properties::json::object();
    scriptedPassProperties.clear();
}

void Renderer::loadScript(const std::filesystem::path& path)
{
    auto&       configManager = *context().config;
    const auto& graphName     = configManager.renderGraphName();
    auto        options       = configManager.getOptions();

    Scripting::Context script;
    script.setObject("__name__", std::string("RenderLab"));

    auto previousDevice = getActivePythonRenderGraphDevice();
    setActivePythonRenderGraphDevice(device);
    Scripting::runScriptFromFile(path, script);

    const auto functionName = RenderGraphIR::getFuncName(graphName);
    auto*      factory      = script.getObject<PyObject*>(functionName);
    CHECK(PyFunction_Check(factory), "graph '{}' must define '{}'", graphName, functionName);

    parameterDefaults = Properties::json::object();
    if (auto* defaults = PyFunction_GetKwDefaults(factory))
    {
        const Properties properties(defaults);
        CHECK(!PyErr_Occurred(), "failed to read parameters of '{}': {}", graphName, pythonError());
        parameterDefaults = properties.toJson();
    }
    parameters = resolveParameters(parameterDefaults, options.at("parameters"));

    const PythonObject arguments{PyTuple_New(0)};
    const PythonObject keywords{Properties(parameters).toPython()};
    CHECK(arguments && keywords, "{}", pythonError());
    const PythonObject graph{PyObject_Call(factory, arguments.get(), keywords.get())};
    CHECK(graph, "failed to build graph '{}': {}", graphName, pythonError());
    const auto loaded = ScriptBindings::fromPython<ref<RenderGraph>>(graph.get());
    CHECK(loaded && *loaded, "graph function '{}' must return a named RenderGraph", functionName);
    const auto& declared = (*loaded)->getName();
    CHECK(
        graphName == declared,
        "'{}' returned graph '{}' while '{}' was configured",
        functionName,
        declared,
        graphName
    );
    setActivePythonRenderGraphDevice(std::move(previousDevice));

    scriptedPassProperties.clear();
    applyPassProperties(**loaded, options.at("passes"));
    setActiveGraph(*loaded);

    appliedParameters = std::move(options.at("parameters"));
    pendingOptions.reset();
}

void Renderer::setActiveGraph(const ref<RenderGraph>& renderGraph)
{

    CHECK(renderGraph->getOutputCount(), "graph '{}' requires a marked color output", renderGraph->getName());
    mpRenderGraph = renderGraph;
    mpRenderGraph->onResize({surface->getConfig()->width, surface->getConfig()->height}, surface->getConfig()->format);
}

void Renderer::initBlit()
{
    rhi::ColorTargetDesc color;
    color.format = surface->getConfig()->format;
    rhi::RenderPipelineDesc desc;
    desc.program                       = shaders.program(shaders.getOrLoad("blit"));
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(device->createRenderPipeline(desc, blitPipeline.writeRef()));
}

void Renderer::recordPresentation(
    rhi::ICommandEncoder& commands, rhi::ITexture& backBuffer, const EditorFrameInput& editor
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
    rhiCheck(root["sourceImage"].setBinding(source->getDefaultView()));
    rhiCheck(root["pointSampler"].setBinding(resources.sampler(BuiltinAssets::Sampler::nearestClamp)));
    const auto& size = backBuffer.getDesc().size;
    pass->setRenderState(ViewportRect{0, 0, float(size.width), float(size.height)}.renderState());
    pass->draw({3});
    if (editor.recordUi)
        editor.recordUi(*pass, renderContext.frameIndex);
    pass->end();
}

const nlohmann::json& Renderer::getScriptedPassProperties(const RenderPass& pass)
{
    const auto [entry, inserted] = scriptedPassProperties.try_emplace({pass.getType(), pass.getName()});
    if (inserted)
        entry->second = nlohmann::json(pass.getProperties().toJson());
    return entry->second;
}

void Renderer::addOption(std::string_view name, nlohmann::json value)
{
    context().config->getSettings().addOption(name, value);
    onOptionsChange();
}

void Renderer::addOptions(const nlohmann::json& options)
{
    context().config->getSettings().addOptions(options);
    onOptionsChange();
}

void Renderer::clearOptions()
{
    context().config->getSettings().clearOptions();
    onOptionsChange();
}

void Renderer::onOptionsChange()
{
    const auto& config = *context().config;
    if (mpRenderGraph && mpRenderGraph->getName() == config.renderGraphName())
        pendingOptions = config.getOptions();
}

void Renderer::applyConfiguration()
{
    const auto& config = *context().config;
    const bool  reload = !mpRenderGraph || mpRenderGraph->getName() != config.renderGraphName() ||
        (pendingOptions && pendingOptions->at("parameters") != appliedParameters);
    if (reload)
    {
        if (commandResources)
            waitIdle();
        loadScript(config.renderGraphScript());
    }

    if (pendingOptions)
    {
        applyPassProperties(*mpRenderGraph, pendingOptions->at("passes"));
        pendingOptions.reset();
    }


    const bool resetFrameResources = !commandResources || mpRenderGraph->needsCompilation();
    if (!resetFrameResources)
        return;
    waitIdle();
    commandResources = std::make_unique<GpuResources>(*device);
    renderContext.gpuResources   = commandResources.get();
    renderContext.frameIndex     = 0;
    preparedScene.tlas           = nullptr;

    materialTable.setup(*commandResources, *device);
    initBlit();
    std::string log;
    CHECK(
        mpRenderGraph->compile(&renderContext, log),
        "failed to compile render graph '{}': {}",
        mpRenderGraph->getName(),
        log
    );
    activeFramesInFlight = mpRenderGraph->getMaxFramesInFlight();
    if (is_set(mpRenderGraph->getSceneInputs(), SceneInputs::RayTracing))
        preparedScene.rays.setup(*commandResources, *device);

    const auto color = mpRenderGraph->getOutputTexture(0);
    CHECK(
        color && color->getDesc().type == rhi::TextureType::Texture2D &&
            rhi::is_set(color->getDesc().usage, rhi::TextureUsage::ShaderResource),
        "first output '{}' of graph '{}' must be a shader-readable color texture",
        mpRenderGraph->getOutputName(0),
        mpRenderGraph->getName()
    );
}

void Renderer::applyPassProperties(RenderGraph& renderGraph, const nlohmann::json& properties)
{
    for (const auto& [type, instances] : properties.items())
    {
        CHECK(instances.is_object(), "options for pass type '{}' must be an object", type);
        for (const auto& [name, values] : instances.items())
        {
            CHECK(values.is_object(), "options for pass '{}' must be an object", name);
            if (!renderGraph.doesPassExist(name))
                continue;

            const auto& pass = *renderGraph.getPass(name);
            if (pass.getType() == type)
                getScriptedPassProperties(pass);
        }
    }

    for (const auto& [key, scripted] : scriptedPassProperties)
    {
        const auto& [type, name] = key;
        if (!renderGraph.doesPassExist(name))
            continue;

        auto& pass = *renderGraph.getPass(name);
        if (pass.getType() != type)
            continue;

        auto values = scripted;
        if (const auto byType = properties.find(type); byType != properties.end())
            if (const auto byName = byType->find(name); byName != byType->end())
                values.update(*byName, true);
        if (values != nlohmann::json(pass.getProperties().toJson()))
            pass.setProperties(Properties::json(std::move(values)));
    }
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
}
void Renderer::resizeSurface()
{
    waitIdle();
    renderContext.gpuResources = nullptr;
    commandResources.reset();
    surface.setNull();
    initSurface();
    mpRenderGraph->onResize({surface->getConfig()->width, surface->getConfig()->height}, surface->getConfig()->format);
    applyConfiguration();
}

const PreparedScene& Renderer::loadSceneAssets(const SceneRenderData& scene, SceneInputs sceneInputs)
{
    auto& assets = *context().assetManager;

    preparedScene.sceneGeneration = scene.sceneGeneration;
    preparedScene.drawItems       = std::span{scene.drawItems};
    preparedScene.shading         = &shading.prepare(
        scene.drawItems, scene.lights, assets, resources, materialTable, is_set(sceneInputs, SceneInputs::RayTracing)
    );
    preparedScene.environmentMap = scene.environmentMap ? &resources.environmentMap(*scene.environmentMap) : nullptr;
    return preparedScene;
}

RenderFrameResult Renderer::render(
    const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds
)
{
    // 获取最新渲染配置
    applyConfiguration();

    const uint64_t submissionValue = lastSubmittedFrame + 1;
    if (submissionValue > activeFramesInFlight)
        waitForFrame(submissionValue - activeFramesInFlight);

    // Texture initialization can submit work to the graphics queue.
    const auto sceneInputs = mpRenderGraph->getSceneInputs();
    loadSceneAssets(scene, sceneInputs);

    rhi::ComPtr<rhi::ITexture> backBuffer;
    const auto                 acquired = surface->acquireNextImage(backBuffer.writeRef());

    if (SLANG_FAILED(acquired))
    {
        const auto [width, height] = context().window->framebufferSize();
        const auto* config         = surface->getConfig();
        if (uint32_t(width) != config->width || uint32_t(height) != config->height)
            return RenderFrameResult::SwapchainNeedsRebuild;
        rhiCheck(acquired);
    }
    CHECK(backBuffer);

    rhi::ComPtr<rhi::ICommandEncoder> commands;
    rhiCheck(queue->createCommandEncoder(commands.writeRef()));

    renderContext.commands     = commands.get();
    renderContext.view         = &view;
    renderContext.editor       = &editor;
    renderContext.deltaSeconds = deltaSeconds;
    preparedScene.prepare(renderContext, sceneInputs, *context().assetManager, sceneAccelerationStructures);
    mpRenderGraph->execute(&renderContext);
    renderContext.commands = nullptr;
    renderContext.view     = nullptr;
    renderContext.editor   = nullptr;

    recordPresentation(*commands, *backBuffer, editor);

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
    rhiCheck(surface->present());

    if (!renderContext.onGpuComplete.empty())
    {
        waitForFrame(submissionValue);
        renderContext.notifyGpuComplete();
    }

    renderContext.frameIndex = (renderContext.frameIndex + 1) % activeFramesInFlight;
    return RenderFrameResult::Presented;
}
