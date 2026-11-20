#include "render/Renderer.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/Visibility.h"
#include "scene/system/SceneRenderData.h"

#include "asset/ApplyOptionalFields.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Serializer.h"
#include "core/Window.h"
#include "core/profiling/Profiler.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VkCheck.h"
#include "render/graph/PassPorts.h"
#include "render/graph/PythonGraph.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/Buffer.h"

#include <cstring>
#include <glm/geometric.hpp>
#include <limits>
#include <span>
#include <utility>

namespace
{
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

    auto& ctx = renderContext();
    DCHECK(!ctx.shaderManager && !ctx.device && !ctx.resources && !ctx.materialTable);
    DCHECK(context().window);

    auto& window = *context().window;
    vulkan.init(window);
    swapchain.init(vulkan, window);

    shaders.init(vulkan.deviceHandle());
    device.init(vulkan, shaders);

    ctx = {
        .shaderManager = &shaders,
        .device        = &device,
        .resources     = &resources,
        .materialTable = &materialTable,
    };

    for (auto& frame : frames)
    {
        frame.init(vulkan);
    }

    for (auto& data : frameStorage)
        data.init(vulkan, device);

    resources.init(vulkan, *context().assetManager, *context().assetDataManager);
    initLightingResources();
    materialTable.init(resources, vulkan, device);
    graph.init(vulkan);
    pluginManager.loadAllPlugins();

    DEBUG_EXEC(inited = true);
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
        vkCheck(vulkan.deviceHandle().waitIdle());
    }
}

void Renderer::shutdown() noexcept
{
    DCHECK(inited);
    waitIdle();

    graph.reset();
    graphCompiler.clear();
    pluginManager.clear();
    sceneAccelerationStructures.reset();
    pickingReadback = {};
    lightBuffer     = {};
    presentTarget   = {};
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
    device.shutdown();
    shaders.shutdown();
    renderContext() = {};
    swapchain.reset();
    vulkan.reset();
    frameIndex           = 0;
    activeFramesInFlight = maxFramesInFlight;
    graphDefinition = nullptr;
    appliedParameterRevision = 0;
    passOverridesChanged = false;
    graphShowsObjectList = false;
    editorControls.clear();
    rendererConfig = nullptr;
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
    if (context().profiler)
        context().profiler->clearGpu();
    for (auto& frame : frames)
        frame.gpuProfiler().discard();
    auto& configManager = *context().config;
    const auto& config = *rendererConfig;
    const auto& definition = configManager.renderGraph();
    CHECK(!definition.requiresRayTracing() || vulkan.supportsRayTracing(), "graph '{}' requires ray tracing", definition.id);
    const auto script = configManager.renderGraphScript();
    const auto description = loadRenderGraph(script, configManager.graphParameterOverrides());
    const auto parameters = description.find("parameters");
    const auto controls = description.find("editorControls");
    const auto objectList = description.find("showObjectList");
    CHECK(
        parameters != description.end() && controls != description.end() &&
            objectList != description.end() && objectList->is_boolean(),
        "invalid parameters or editor settings for render graph '{}'",
        script.string()
    );
    configManager.applyGraphParameters(*parameters);
    Deserialize(*controls, editorControls);
    graphShowsObjectList = objectList->get<bool>();
    if (graphDefinition != &definition)
        graphCompiler.clear();

    activeFramesInFlight = config.renderPath == RenderPath::OfflinePathTracing ? 1u : maxFramesInFlight;
    frameIndex           = 0;

    materialTable.setup(graph);
    lightBuffer       = graph.createBuffer({
              .size  = kMaxLights * sizeof(LightData),
              .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    const auto extent = swapchain.extent();
    presentTarget     = graph.createExternalImage({
            .format = swapchain.surfaceFormat().format,
            .extent = {extent.width, extent.height, 1},
    });
    if (config.feature.hdr)
        CHECK(swapchain.surfaceFormat().colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);

    const PassSetup setup{vulkan, extent, frameStorage, config};
    GraphResources inputs{
        {"presentTarget", presentTarget},
        {"lightBuffer", lightBuffer},
    };
    if (graphUsesInputPrefix(description, "emptyShadow_"))
        pass_ports::insert(inputs, "emptyShadow_", createUnshadowedResources(graph));
    const auto outputs = graphCompiler.compile(graph, setup, description, inputs, pluginManager,
                                               configManager.passOverrides());
    const auto finalColor = outputs.find("color");
    CHECK(finalColor != outputs.end(), "render graph '{}' must mark a color output", script.string());
    CHECK(std::holds_alternative<RenderGraph::ImageHandle>(finalColor->second), "graph color output must be an image");
    const auto color = std::get<RenderGraph::ImageHandle>(finalColor->second);
    CHECK(
        color.id != presentTarget.id && graph.sharesImage(color, presentTarget),
        "render graph '{}' must write its color output to presentTarget",
        script.string()
    );
    const auto unsupportedUsage = graph.imageUsage(presentTarget) & ~swapchain.imageUsage();
    CHECK(
        !unsupportedUsage,
        "render graph '{}' requests unsupported swapchain usage {}",
        script.string(),
        vk::to_string(unsupportedUsage)
    );

    const auto readback = outputs.find("pickingReadback");
    CHECK(readback == outputs.end() || std::holds_alternative<RenderGraph::BufferHandle>(readback->second),
          "picking readback output must be a buffer");
    pickingReadback = readback == outputs.end() ? RenderGraph::BufferHandle{} :
        std::get<RenderGraph::BufferHandle>(readback->second);
    graph.addStage("Present", endPass, setup, color, pickingReadback);
    graph.compile(setup);
    for (auto& data : frameStorage)
    {
        data.writeLightBufferDescriptor(graph.getBuffer(lightBuffer));
        data.uploadShadowUniforms({.enabled = 0u});
    }

    if (outputs.contains("pointLightShadowMap"))
        bindShadowResources(graph, pass_ports::read<ShadowResources>(outputs), frameStorage, resources);

    graphDefinition      = &definition;
    appliedParameterRevision = configManager.graphParameterRevision();
    passOverridesChanged = false;
}

ExposureConfig Renderer::defaultExposure() const
{
    for (const auto* instance : graphCompiler.activePasses())
        if (const auto* exposure = dynamic_cast<const ExposureProvider*>(instance->pass.get()))
            return exposure->exposureDefaults();
    return {};
}

void Renderer::setPassOverrides(std::string_view type, std::string_view instance, nlohmann::json properties)
{
    CHECK(properties.is_object(), "pass overrides for '{}' must be an object", instance);
    auto& overrides = context().config->passOverrides();
    if (properties.empty())
    {
        const auto byType = overrides.find(std::string{type});
        if (byType != overrides.end())
        {
            byType->second.erase(std::string{instance});
            if (byType->second.empty())
                overrides.erase(byType);
        }
    }
    else
        overrides[std::string{type}].insert_or_assign(std::string{instance}, std::move(properties));
    passOverridesChanged = true;
}

void Renderer::applyConfiguration()
{
    auto& config = *context().config;
    if (graphDefinition != &config.renderGraph() || appliedParameterRevision != config.graphParameterRevision() ||
        (passOverridesChanged && graphCompiler.applyOverrides(config.passOverrides())))
        rebuildGraph();
    passOverridesChanged = false;
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

const RenderData& Renderer::prepare(const RenderView& view, const SceneRenderData& scene, uint32_t targetFrameIndex)
{
    graph.beginFrame(targetFrameIndex);
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
            const auto upload = graph.allocateUpload(lightBuffer, bytes);
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

    // wait fence
    auto& frame = currentFrame();
    frame.wait();

    // 从swapchain取image
    const auto [acquireResult, imageIndex] = swapchain.acquire(frame.imageAvailableSemaphore());
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        return {.swapchainNeedsRebuild = true};

    graph.bindImage(presentTarget, swapchain.image(imageIndex));

    // 全局prepare：更新场景数据
    const auto& renderData = prepare(view, scene, frameIndex);

    auto&           data = frameStorage[frameIndex];
    const PassSetup setup{vulkan, swapchain.extent(), frameStorage, *rendererConfig};
    PassFrame       execution{setup, data, view, renderData, editor, frameIndex, deltaSeconds};

    // 逐pass prepare
    graph.prepare(execution);

    // pass执行
    {
        auto*           profiler = context().profiler;
        CpuProfileScope timing{profiler, "Command recording"};
        auto&           commandBuffer = frame.commandBufferHandle();
        vkCheck(commandBuffer.reset());
        vkCheck(commandBuffer.begin({}));

        auto& gpuProfiler = frame.gpuProfiler();
        graph.beginGpuProfiling(commandBuffer, gpuProfiler, profiler);
        if (graphDefinition->requiresRayTracing())
        {
            CpuProfileScope cpuTiming{profiler, "Scene acceleration structures"};
            GpuProfileScope gpuTiming{&gpuProfiler, commandBuffer, "Scene acceleration structures"};
            execution.sceneTlas = &sceneAccelerationStructures.recordBuilds(
                vulkan, commandBuffer, resources, renderData, frameIndex
            );
        }
        graph.execute(commandBuffer, execution, gpuProfiler, profiler);
        gpuProfiler.endFrame(commandBuffer);
        vkCheck(commandBuffer.end());
    }


    frame.submit(swapchain.renderFinishedSemaphore(imageIndex), graph.firstUseStages(presentTarget));
    const auto presentResult = swapchain.present(imageIndex);

    RenderFrameResult frameResult{
        .swapchainNeedsRebuild = acquireResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR,
    };

    // 直接同步等待，获取点击结果
    if (editor.requestPick && supportsObjectPicking())
    {
        CpuProfileScope timing{context().profiler, "Picking wait/readback"};
        frame.wait();

        graph.getBuffer(pickingReadback)
            .download(&frameResult.pickedSelectionId, sizeof(frameResult.pickedSelectionId));
        frameResult.hasPickResult = true;
    }

    frameIndex = (frameIndex + 1) % activeFramesInFlight;
    return frameResult;
}
