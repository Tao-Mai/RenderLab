#include "render/device/ResourceBarriers.h"
#include "render/Renderer.h"
#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/Visibility.h"
#include "scene/system/SceneRenderData.h"

#include "asset/ApplyOptionalFields.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/GlobalState.h"
#include "core/Logger.h"
#include "core/Scripting.h"
#include "core/Serializer.h"
#include "core/Window.h"
#include "core/profiling/Profiler.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VkCheck.h"
#include "render/graph/RenderGraphIR.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/postprocess/ExposureState.h"
#include "render/raster/RasterAttachments.h"
#include "render/resources/Buffer.h"
#include "render/resources/ShaderLayout.h"
#include "scene/SceneManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/geometric.hpp>
#include <limits>
#include <span>
#include <utility>

namespace
{
Falcor::Properties::json resolveParameters(const Falcor::Properties::json& defaults, const nlohmann::json& overrides)
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
            CHECK(edited->is_number() && std::isfinite(edited->get<double>()), "expected a finite number for '{}'", name);
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

LightRanges groupLights(std::span<const RenderLight> source, std::vector<LightData>& grouped)
{
    grouped.clear();
    grouped.reserve(source.size());
    const auto append = [&](GpuLightType type)
    {
        for (const auto& light : source)
            if (light.enabled && light.data.type() == type)
                grouped.push_back(light.data);
        return static_cast<uint32_t>(grouped.size());
    };

    LightRanges ranges;
    ranges.directionalEnd = append(GpuLightType::Directional);
    ranges.pointEnd       = append(GpuLightType::Point);
    ranges.rectAreaEnd    = append(GpuLightType::RectArea);
    ranges.spotEnd        = append(GpuLightType::Spot);
    CHECK(grouped.size() <= kMaxLights, "scene has {} enabled lights; maximum is {}", grouped.size(), kMaxLights);
    return ranges;
}
}  // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() { DCHECK(!inited); }

void Renderer::init()
{
    DCHECK(!inited);
    rendererConfig = &context().config->rendererConfig();

    DCHECK(context().window);

    auto& window = *context().window;
    vulkan.init(window);

    shaders.init(vulkan.deviceHandle());
    device->init(vulkan, shaders);
    swapchain.init(vulkan, window);

    auto& renderCommands = *device->getRenderContext();
    renderCommands.resources        = &resources;
    renderCommands.materialTable    = &materialTable;
    renderCommands.offlinePipelines = &offlinePipelines;
    renderCommands.config           = rendererConfig;
    renderCommands.frames           = frameStorage;

    for (auto& frame : frames)
    {
        frame.init(vulkan);
    }

    for (auto& data : frameStorage)
        data.init(vulkan, *device);

    resources.init(vulkan, *context().assetManager, *context().assetDataManager);
    initLightingResources();
    materialTable.init(resources, vulkan, *device);

    DEBUG_EXEC(inited = true);
    waitIdle();
    applyConfiguration();
}

void Renderer::initLightingResources()
{
    brdfLutTexture     = resources.texture(BuiltinAssets::Texture::kBrdfLut);
    sheenLutTexture    = resources.texture(BuiltinAssets::Texture::kSheenLut);
    const auto sampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    for (auto& data : frameStorage)
        data.writeLutDescriptors(brdfLutTexture->imageView(), sheenLutTexture->imageView(), sampler);

    bindEnvironmentTextures();
}

void Renderer::updateEnvironmentBindings(const std::optional<EnvironmentMapAsset::ID>& id)
{
    if (environmentMap == id)
        return;

    waitIdle();
    environmentMap = id;
    bindEnvironmentTextures();
}

void Renderer::bindEnvironmentTextures()
{
    TextureBinding irradianceBinding{BuiltinAssets::Texture::kBlackCube, BuiltinAssets::Sampler::linearClamp};
    TextureBinding prefilteredBinding = irradianceBinding;
    TextureBinding radianceBinding    = irradianceBinding;
    if (environmentMap)
    {
        const auto& assets      = *context().assetManager;
        const auto& environment = assets.get<EnvironmentMapAsset>(*environmentMap);
        CHECK(
            !environment.irradiance.textureID.empty() && !environment.irradiance.samplerID.empty() &&
                !environment.prefilteredSpecular.textureID.empty() &&
                !environment.prefilteredSpecular.samplerID.empty(),
            "environment '{}' requires irradiance and prefiltered specular textures",
            environment.id
        );
        radianceBinding      = environment.radiance;
        irradianceBinding    = environment.irradiance;
        prefilteredBinding   = environment.prefilteredSpecular;
        const auto& radiance = assets.get<TextureAsset>(radianceBinding.textureID);
        CHECK(
            radiance.layout == ImageLayout::Cubemap && radiance.colorSpace == ColorSpace::Linear,
            "environment '{}' requires a linear radiance cubemap",
            environment.id
        );
        const auto& irradiance  = assets.get<TextureAsset>(irradianceBinding.textureID);
        const auto& prefiltered = assets.get<TextureAsset>(prefilteredBinding.textureID);
        CHECK(
            irradiance.layout == ImageLayout::Cubemap && irradiance.colorSpace == ColorSpace::Linear &&
                prefiltered.layout == ImageLayout::Cubemap && prefiltered.colorSpace == ColorSpace::Linear &&
                prefiltered.mipLevels > 0,
            "environment '{}' requires linear irradiance and prefiltered cubemaps",
            environment.id
        );
    }

    radianceTexture            = resources.texture(radianceBinding.textureID);
    irradianceTexture          = resources.texture(irradianceBinding.textureID);
    prefilteredSpecularTexture = resources.texture(prefilteredBinding.textureID);
    prefilteredMaxLod          = static_cast<float>(prefilteredSpecularTexture->imageInfo().mipLevels - 1);
    const vk::DescriptorImageInfo irradiance{
        .sampler     = resources.sampler(irradianceBinding.samplerID),
        .imageView   = irradianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo prefiltered{
        .sampler     = resources.sampler(prefilteredBinding.samplerID),
        .imageView   = prefilteredSpecularTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo radiance{
        .sampler     = resources.sampler(radianceBinding.samplerID),
        .imageView   = radianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    for (auto& data : frameStorage)
        data.writeEnvironmentDescriptors(irradiance, prefiltered, radiance);
}

void Renderer::waitIdle()
{
    CpuProfileScope timing{context().profiler, "Device idle wait"};
    if (*vulkan.deviceHandle())
    {
        device->wait();
    }
}

void Renderer::shutdown() noexcept
{
    DCHECK(inited);
    waitIdle();

    exposurePass = {};
    mpRenderGraph = {};
    commandResources.reset();
    lightResource.reset();
    offlinePipelines.clear();
    sceneAccelerationStructures.reset();
    pickingOutput = {};
    pickingReadback = {};
    lightBuffer     = {};
    blitSets.clear();
    blitSetLayout = {};
    blitPipelineLayout = nullptr;
    blitPipeline = nullptr;
    renderStorage   = {};
    materialTable   = {};
    offlineScene    = {};
    for (FrameContext& frame : frames)
    {
        frame.reset();
    }
    for (auto& data : frameStorage)
        data.reset();
    radianceTexture.reset();
    irradianceTexture.reset();
    prefilteredSpecularTexture.reset();
    brdfLutTexture.reset();
    sheenLutTexture.reset();
    environmentMap.reset();
    prefilteredMaxLod = 0.0f;
    resources.shutdown();
    swapchain.reset();
    device->shutdown();
    shaders.shutdown();
    vulkan.reset();
    frameIndex           = 0;
    activeFramesInFlight = maxFramesInFlight;
    appliedParameters = {};
    pendingOptions.reset();
    parameterDefaults = Falcor::Properties::json::object();
    parameters = Falcor::Properties::json::object();
    scriptedPassProperties.clear();
    rendererConfig = nullptr;
    DEBUG_EXEC(inited = false);
}

void Renderer::loadScript(const std::filesystem::path& path)
{
    auto& configManager = *context().config;
    const auto& graphName = configManager.renderGraphName();
    const auto renderPath = configManager.rendererConfig().renderPath;
    auto options = configManager.getOptions();
    CHECK(!requiresRayTracing(renderPath) || vulkan.supportsRayTracing(), "graph '{}' requires ray tracing", graphName);

    Falcor::Scripting::Context script;
    script.setObject("__name__", std::string("RenderLab"));

    auto previousDevice = Falcor::getActivePythonRenderGraphDevice();
    Falcor::setActivePythonRenderGraphDevice(device);
    Falcor::Scripting::runScriptFromFile(path, script);

    const auto functionName = Falcor::RenderGraphIR::getFuncName(graphName);
    auto* factory = script.getObject<PyObject*>(functionName);
    CHECK(PyFunction_Check(factory), "graph '{}' must define '{}'", graphName, functionName);

    parameterDefaults = Falcor::Properties::json::object();
    if (auto* defaults = PyFunction_GetKwDefaults(factory))
    {
        const Falcor::Properties properties(defaults);
        CHECK(!PyErr_Occurred(), "failed to read parameters of '{}': {}", graphName, Falcor::pythonError());
        parameterDefaults = properties.toJson();
    }
    parameters = resolveParameters(parameterDefaults, options.at("parameters"));
    configManager.applyGraphParameters(nlohmann::json(parameters));

    const Falcor::PythonObject arguments{PyTuple_New(0)};
    const Falcor::PythonObject keywords{Falcor::Properties(parameters).toPython()};
    CHECK(arguments && keywords, "{}", Falcor::pythonError());
    const Falcor::PythonObject graph{PyObject_Call(factory, arguments.get(), keywords.get())};
    CHECK(graph, "failed to build graph '{}': {}", graphName, Falcor::pythonError());
    const auto loaded = Falcor::ScriptBindings::fromPython<Falcor::ref<Falcor::RenderGraph>>(graph.get());
    CHECK(loaded && *loaded, "graph function '{}' must return a named RenderGraph", functionName);
    const auto& declared = (*loaded)->getName();
    CHECK(graphName == declared, "'{}' returned graph '{}' while '{}' was configured", functionName, declared, graphName);
    Falcor::setActivePythonRenderGraphDevice(std::move(previousDevice));

    scriptedPassProperties.clear();
    applyPassProperties(**loaded, options.at("passes"));
    setActiveGraph(*loaded);

    appliedParameters = std::move(options.at("parameters"));
    pendingOptions.reset();
}

void Renderer::setActiveGraph(const Falcor::ref<Falcor::RenderGraph>& renderGraph)
{
    DCHECK(renderGraph);
    CHECK(renderGraph->getOutputCount(), "graph '{}' requires a marked color output", renderGraph->getName());
    mpRenderGraph = renderGraph;
    mpRenderGraph->onResize(&swapchain.getFbo(0));
    if (const auto* scenes = context().sceneManager)
        mpRenderGraph->setScene(scenes->getScene());
}

void Renderer::initFrameResources()
{
    const auto& config = *rendererConfig;
    activeFramesInFlight = config.renderPath == RenderPath::OfflinePathTracing ? 1u : maxFramesInFlight;
    frameIndex           = 0;

    lightResource = device->createBuffer(
        kMaxLights * sizeof(LightData), Falcor::ResourceBindFlags::ShaderResource, Falcor::MemoryType::DeviceLocal
    );

    auto& cache = *commandResources;
    lightBuffer = cache.registerBuffer(lightResource);
    materialTable.setup(cache, vulkan);
    for (auto& data : frameStorage)
        data.writeLightBufferDescriptor(cache, lightBuffer);
}

void Renderer::initBlit()
{
    const auto shader = shaders.getOrLoad("blit");
    if (!blitSetLayout)
    {
        const auto& layout = shaders.layout(shader);
        const auto source = layout.binding<vk::DescriptorType::eSampledImage>("sourceImage", 0);
        const auto sampler = layout.binding<vk::DescriptorType::eSampler>("pointSampler", 0);
        blitSourceBinding = source.index;

        constexpr auto kFragment = vk::ShaderStageFlagBits::eFragment;
        const std::array bindings{source.layout(kFragment), sampler.layout(kFragment)};
        blitSetLayout = device->createDescriptorSetLayout(bindings);
        const std::array sets{&blitSetLayout};
        blitPipelineLayout = device->getOrCreatePipelineLayout(sets, {});
        const auto pointSampler = resources.sampler(BuiltinAssets::Sampler::nearestClamp);
        blitSets.reserve(maxFramesInFlight);
        for (uint32_t index = 0; index < maxFramesInFlight; ++index)
        {
            auto& set = blitSets.emplace_back(device->allocateDescriptorSet(blitSetLayout));
            commandResources->writeDescriptor(*set, sampler.index, pointSampler);
        }
    }

    blitPipeline = device->getOrCreatePipeline({
        .shader = shader,
        .layout = blitPipelineLayout,
        .state = {.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
        .colorFormats = {swapchain.surfaceFormat().format},
    });
}

void Renderer::endFrame(Falcor::RenderContext& context, VulkanResources::ImageHandle backBuffer)
{
    auto& cache = *context.vulkanResources;
    auto& commands = *context.commands;
    const auto source = cache.registerImage(mpRenderGraph->getOutput(0)->asTexture());
    constexpr auto kRead = vulkan_access::sampled(vk::PipelineStageFlagBits2::eFragmentShader);
    cache.declareAccess({source.id, kRead});
    cache.transition(commands, source, kRead);
    cache.transition(commands, backBuffer, vulkan_access::kColorReadWrite);

    const auto set = *blitSets[context.frameIndex];
    cache.writeDescriptor(set, blitSourceBinding, source);
    const RasterAttachments<1> attachments{{RasterColorAttachment{backBuffer, vk::AttachmentLoadOp::eDontCare}}};
    attachments.begin(cache, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, blitPipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, blitPipelineLayout, 0, std::array{set}, {});
    commands.draw(3, 1, 0, 0);
    if (context.editor->recordUi)
        context.editor->recordUi(*commands);
    commands.endRendering();
}

ExposureConfig Renderer::defaultExposure() const
{
    ExposureConfig settings;
    if (exposurePass)
        Deserialize(nlohmann::json(exposurePass->getProperties().toJson()), settings);
    return settings;
}

const nlohmann::json& Renderer::getScriptedPassProperties(const Falcor::RenderPass& pass)
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
    const bool reload = !mpRenderGraph || mpRenderGraph->getName() != config.renderGraphName() ||
        (pendingOptions && pendingOptions->at("parameters") != appliedParameters);
    std::optional<CpuProfileScope> timing;
    if (reload)
    {
        timing.emplace(context().profiler, "Graph rebuild");
        if (commandResources)
            waitIdle();
        loadScript(config.renderGraphScript());
    }
    else if (const auto* scenes = context().sceneManager)
        mpRenderGraph->setScene(scenes->getScene());

    if (pendingOptions)
    {
        applyPassProperties(*mpRenderGraph, pendingOptions->at("passes"));
        pendingOptions.reset();
    }

    const bool resetFrameResources = reload || !commandResources;
    if (resetFrameResources)
    {
        if (rendererConfig->feature.hdr)
            CHECK(swapchain.surfaceFormat().colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
        commandResources = std::make_unique<VulkanResources>(vulkan);
        initBlit();
    }

    auto& cache = *commandResources;
    auto compileContext = *device->getRenderContext();
    compileContext.vulkanResources = &cache;
    const auto idleRevision = vulkan.getIdleRevision();
    std::string log;
    CHECK(
        mpRenderGraph->compile(&compileContext, log),
        "failed to compile render graph '{}': {}",
        mpRenderGraph->getName(),
        log
    );

    const bool waited = idleRevision != vulkan.getIdleRevision();
    if (!resetFrameResources && !waited)
        return;

    if (auto* profiler = context().profiler)
        profiler->clearGpu();
    for (auto& frame : frames)
        frame.gpuProfiler().discard();

    const auto color = mpRenderGraph->getOutput(0)->asTexture();
    CHECK(
        color && color->getType() == Falcor::Resource::Type::Texture2D && color->getArraySize() == 1 &&
            (color->imageInfo().usage & vk::ImageUsageFlagBits::eSampled),
        "graph '{}' must mark a shader-readable color texture as its first output",
        mpRenderGraph->getName()
    );

    exposurePass = {};
    pickingOutput = {};
    for (size_t index = 1; index < mpRenderGraph->getOutputCount(); ++index)
    {
        const auto name = mpRenderGraph->getOutputName(index);
        const auto separator = name.rfind('.');
        const auto field = std::string_view{name}.substr(separator + 1);
        if (field == kExposureStateOutput)
        {
            const auto& pass = mpRenderGraph->getPass(name.substr(0, separator));
            if (pass->getType() == kExposurePassType)
            {
                CHECK(!exposurePass, "a render graph can only mark one exposure state");
                exposurePass = pass;
            }
            continue;
        }
        if (field != kPickingImageOutput)
            continue;

        const auto resource = mpRenderGraph->getOutput(name);
        auto texture = resource ? resource->asTexture() : nullptr;
        CHECK(
            texture && texture->getType() == Falcor::Resource::Type::Texture2D && texture->getArraySize() == 1 &&
                texture->getSampleCount() == 1 && texture->getFormat() == Falcor::ResourceFormat::R32Uint &&
                (texture->imageInfo().usage & vk::ImageUsageFlagBits::eTransferSrc),
            "picking output '{}' must be a single-sample R32Uint texture readable by a transfer",
            name
        );
        CHECK(!pickingOutput, "a render graph can only mark one picking image");
        pickingOutput = std::move(texture);
    }
    if (pickingOutput && !pickingReadback)
    {
        pickingReadback = device->createBuffer(
            sizeof(uint32_t), Falcor::ResourceBindFlags::None, Falcor::MemoryType::ReadBack
        );
    }

    if (waited)
        cache.releaseUnusedResources();
    if (resetFrameResources)
        initFrameResources();
}

void Renderer::applyPassProperties(Falcor::RenderGraph& renderGraph, const nlohmann::json& properties)
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
            pass.setProperties(Falcor::Properties::json(std::move(values)));
    }
}

void Renderer::recreateSwapchain()
{
    DCHECK(inited);
    Window& window = *context().window;
    DEBUG_ONLY(const auto [width, height] = window.framebufferSize();)
    DCHECK(width > 0 && height > 0);
    waitIdle();
    commandResources.reset();
    swapchain.reset();
    swapchain.init(vulkan, window);

    mpRenderGraph->onResize(&swapchain.getFbo(0));
    applyConfiguration();
}

const PreparedScene& Renderer::prepare(const RenderView& view, const SceneRenderData& scene, uint32_t targetFrameIndex)
{
    auto& cache = *commandResources;
    cache.beginFrame(targetFrameIndex);
    const auto& config        = *rendererConfig;
    const bool  rasterization = config.renderPath == RenderPath::Forward || config.renderPath == RenderPath::Deferred;
    const bool  offline = config.renderPath == RenderPath::OfflinePathTracing;
    auto&       assets        = *context().assetManager;
    auto*       profiler      = context().profiler;
    auto&       renderData    = renderStorage[targetFrameIndex];
    renderData.sceneId        = scene.sceneId;
    renderData.drawItems      = std::span{scene.drawItems};
    renderData.environmentMap = scene.environmentMap;
    renderData.exposure       = scene.exposure;
    renderData.materialIndices.resize(renderData.drawItems.size());
    {
        CpuProfileScope timing{profiler, "Render resources"};
        for (uint32_t itemIndex = 0; itemIndex < renderData.drawItems.size(); ++itemIndex)
        {
            const auto& item = renderData.drawItems[itemIndex];
            // mesh数据读进GPU
            resources.mesh(item.mesh);
            if (offline)
                continue;

            // 记录material在SSBO的idx
            auto                       material = assets.get<MaterialAsset>(item.materialId).as<GltfMaterial>();
            MaterialTable::MaterialKey materialKey{.assetId = item.materialId};
            if (item.materialOverride)
            {
                applyOptionalFields(material, *item.materialOverride);
                materialKey.overrideOwner = {item.selectionId, item.instanceIndex, item.submeshIndex};
            }

            renderData.materialIndices[itemIndex] = materialTable.update(materialKey, material, resources, assets);
        }

        renderData.offlineScene = nullptr;
        if (offline)
        {
            renderData.offlineScene = &offlineScene.prepare(scene.drawItems, scene.offlineLights, assets, resources, materialTable);
            for (uint32_t index = 0; index < renderData.drawItems.size(); ++index)
                renderData.materialIndices[index] = renderData.offlineScene->draws[index].materialIndex;
        }

        // 如果有新纹理或SSBO需要扩容，则更新material descriptor set
        materialTable.upload(vulkan, targetFrameIndex);
        renderData.materialDescriptorSet = materialTable.descriptorSetHandle(targetFrameIndex);
    }

    renderData.lightRanges = groupLights(scene.lights, renderData.lights);

    // 写入shadowIdx
    GpuLightType lightType   = GpuLightType::Point;
    uint32_t     shadowIndex = 0;
    for (auto& light : renderData.lights)
    {
        if (light.type() != lightType)
        {
            lightType   = light.type();
            shadowIndex = 0;
        }
        light.shadowIdx = light.castShadow() ? shadowIndex++ : ~0u;
    }

    renderData.drawBounds.clear();
    renderData.visibilityMasks.clear();

    if (profiler)
        profiler->recordCulling(static_cast<uint32_t>(renderData.drawItems.size()), {});

    // 光栅化视锥剔除
    if (rasterization)
    {
        CpuProfileScope timing{profiler, "Main view preparation"};
        const Frustum   mainFrustum{view.uniforms.viewProjection};

        renderData.drawBounds.reserve(renderData.drawItems.size());
        renderData.visibilityMasks.reserve(renderData.drawItems.size());
        uint32_t mainVisible = 0;
        for (const auto& item : renderData.drawItems)
        {
            const auto& meshAsset   = assets.get<MeshAsset>(item.mesh);
            const auto  worldBounds = meshAsset.bounds.transformed(item.model);
            const bool  visible     = mainFrustum.intersects(worldBounds);
            renderData.drawBounds.push_back(worldBounds);
            renderData.visibilityMasks.push_back(visible ? RenderVisibility::kMainView : 0);
            if (visible)
                ++mainVisible;
        }
        if (profiler)
            profiler->recordCulling(static_cast<uint32_t>(renderData.drawItems.size()), {{"Main view", mainVisible}});
    }

    updateEnvironmentBindings(renderData.environmentMap);

    {
        CpuProfileScope timing{context().profiler, "Frame upload"};
        auto&           data = frameStorage[targetFrameIndex];
        data.uploadSceneUniforms(view.uniforms);
        if (rasterization)
        {
            const auto&         feature = config.feature;
            const LightUniforms lighting{
                .lightRanges = renderData.lightRanges,
                .ambientCoefficient = feature.ambientCoefficient,
                .prefilteredMaxLod = prefilteredMaxLod,
            };
            data.uploadLightUniforms(lighting);
        }

        // 上传lightData
        if (!renderData.lights.empty())
        {
            const auto bytes  = renderData.lights.size() * sizeof(LightData);
            const auto upload = cache.allocateUpload(lightBuffer, bytes);
            std::memcpy(upload.data.data(), renderData.lights.data(), bytes);
        }
    }
    return renderData;
}

RenderFrameResult Renderer::render(
    const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds
)
{
    // 获取最新渲染配置
    applyConfiguration();
    const bool pickRequested = editor.requestPick && supportsObjectPicking();

    // wait fence
    auto& frame = currentFrame();
    frame.wait();

    // 从swapchain取image
    const auto [acquireResult, imageIndex] = swapchain.acquire(frame.imageAvailableSemaphore());
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        return {.swapchainNeedsRebuild = true};

    auto& cache = *commandResources;
    const auto& backBuffer = swapchain.getFbo(imageIndex).getColorTexture(0);
    const auto backBufferImage = cache.registerImage(backBuffer);
    cache.setAcquiredImage(backBufferImage);

    // 全局prepare：更新场景数据
    const bool environmentChanged = environmentMap != scene.environmentMap;
    const auto& renderData = prepare(view, scene, frameIndex);

    // pass执行
    {
        auto*           profiler = context().profiler;
        CpuProfileScope timing{profiler, "Command recording"};
        auto&           commandBuffer = frame.commandBufferHandle();
        vkCheck(commandBuffer.reset());
        vkCheck(commandBuffer.begin({}));

        auto& gpuProfiler = frame.gpuProfiler();
        gpuProfiler.beginFrame(commandBuffer, profiler);
        auto renderCommands = *device->getRenderContext();
        renderCommands.commands = &commandBuffer;
        renderCommands.vulkanResources = &cache;
        renderCommands.frameData = &frameStorage[frameIndex];
        renderCommands.view = &view;
        renderCommands.scene = &renderData;
        renderCommands.editor = &editor;
        renderCommands.gpuProfiler = &gpuProfiler;
        renderCommands.profiler = profiler;
        renderCommands.frameIndex = frameIndex;
        renderCommands.deltaSeconds = deltaSeconds;
        if (requiresRayTracing(rendererConfig->renderPath))
        {
            CpuProfileScope cpuTiming{profiler, "Scene acceleration structures"};
            GpuProfileScope gpuTiming{&gpuProfiler, commandBuffer, "Scene acceleration structures"};
            renderCommands.sceneTlas = &sceneAccelerationStructures.recordBuilds(
                vulkan, commandBuffer, resources, renderData, frameIndex
            );
        }
        if (environmentChanged)
            mpRenderGraph->onSceneUpdates(&renderCommands, Falcor::IScene::UpdateFlags::EnvMapChanged);
        cache.recordUploads(commandBuffer);
        mpRenderGraph->getPassesDictionary()[Falcor::kRenderPassRefreshFlags] =
            environmentChanged ? Falcor::RenderPassRefreshFlags::LightingChanged : Falcor::RenderPassRefreshFlags::None;
        mpRenderGraph->execute(&renderCommands);
        endFrame(renderCommands, backBufferImage);
        cache.transition(commandBuffer, {backBufferImage.id, vulkan_access::kPresent});
        if (pickRequested)
        {
            const auto image = cache.registerImage(pickingOutput);
            const auto buffer = cache.registerBuffer(pickingReadback);
            const vk::BufferImageCopy region{
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageOffset = {static_cast<int32_t>(editor.pickX), static_cast<int32_t>(editor.pickY), 0},
                .imageExtent = {1, 1, 1},
            };
            cache.transition(commandBuffer, image, vulkan_access::kTransferSource);
            cache.transition(commandBuffer, buffer, vulkan_access::kTransferWrite);
            commandBuffer.copyImageToBuffer(
                pickingOutput->handle(), vk::ImageLayout::eTransferSrcOptimal, pickingReadback->handle(), region
            );
            cache.transition(commandBuffer, buffer, vulkan_access::kHostRead);
        }
        gpuProfiler.endFrame(commandBuffer);
        vkCheck(commandBuffer.end());
    }


    const auto acquireStages = cache.firstUseStages(backBufferImage);
    CHECK(acquireStages, "render graph '{}' must access its presentation target", mpRenderGraph->getName());
    frame.submit(swapchain.renderFinishedSemaphore(imageIndex), acquireStages);
    const auto presentResult = swapchain.present(imageIndex);

    RenderFrameResult frameResult{
        .swapchainNeedsRebuild = acquireResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR,
    };

    // 直接同步等待，获取点击结果
    if (pickRequested)
    {
        CpuProfileScope timing{context().profiler, "Picking wait/readback"};
        frame.wait();

        pickingReadback->getBlob(&frameResult.pickedSelectionId, 0, sizeof(frameResult.pickedSelectionId));
        frameResult.hasPickResult = true;
    }

    frameIndex = (frameIndex + 1) % activeFramesInFlight;
    return frameResult;
}
