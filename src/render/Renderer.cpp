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
#include "core/Window.h"
#include "core/profiling/Profiler.h"
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
#include "render/feature/postprocess/BloomPass.h"
#include "render/feature/postprocess/ToneMappingPass.h"
#include "render/feature/shadow/ShadowPass.h"
#include "render/offline/OfflinePathTracingPass.h"
#include "render/raytracing/BlasBuildPass.h"
#include "render/raytracing/RayTracingBlitPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/raytracing/TemporalReprojectionPass.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resource/Buffer.h"

#include <glm/geometric.hpp>
#include <limits>
#include <span>
#include <tuple>
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
    return ranges;
}
}  // namespace

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
        BloomPass,
        ToneMappingPass,
        BlasBuildPass,
        TlasBuildPass,
        RayTracingPass,
        TemporalReprojectionPass,
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
    return graph.addPass(
        passes->get<T>(),
        PassSetup{vulkan, swapchain.extent(), frameStorage, *rendererConfig},
        std::forward<Args>(args)...
    );
}

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

    shaders.init(vulkan.deviceHandle(), *context().assetManager);
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
    graph.init(vulkan, swapchain);
    passes = std::make_unique<Passes>();

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

void Renderer::createShadowSampler()
{
    const auto filter = rendererConfig->feature.shadow.filter == SamplerAsset::Filter::Linear ? vk::Filter::eLinear
                                                                                              : vk::Filter::eNearest;
    shadowSampler     = vkCheck(vulkan.deviceHandle().createSampler({
            .magFilter     = filter,
            .minFilter     = filter,
            .mipmapMode    = vk::SamplerMipmapMode::eNearest,
            .addressModeU  = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV  = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW  = vk::SamplerAddressMode::eClampToEdge,
            .compareEnable = true,
            .compareOp     = vk::CompareOp::eLessOrEqual,
            .maxLod        = 0.0f,
    }));
}

void Renderer::writeShadowDescriptors(ShadowPass::Output shadow)
{
    const auto view = [&](RenderGraph::ImageHandle image, vk::ImageViewType type)
    {
        return graph.getImage(image).getOrCreateImageView({
            .viewType         = type,
            .format           = vk::Format::eD32Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = graph.imageInfo(image).arrayLayers,
            },
        });
    };
    const auto point         = view(shadow.pointLightShadowMap, vk::ImageViewType::eCubeArray);
    const auto spot          = view(shadow.spotLightShadowMap, vk::ImageViewType::e2DArray);
    const auto rectArea      = view(shadow.rectAreaLightShadowMap, vk::ImageViewType::e2DArray);
    const auto directional   = view(shadow.cascadeShadowMap, vk::ImageViewType::e2DArray);
    const auto linearSampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    const auto bufferInfo    = [&](RenderGraph::BufferHandle handle)
    {
        const auto& buffer = graph.getBuffer(handle, 0);
        return vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
    };
    const std::array shadowBuffers{
        bufferInfo(shadow.pointShadowBuffer),
        bufferInfo(shadow.spotShadowBuffer),
        bufferInfo(shadow.rectAreaShadowBuffer),
        bufferInfo(shadow.directionalShadowBuffer),
    };
    for (auto& data : frameStorage)
    {
        data.writeShadowBufferDescriptors(shadowBuffers);
        data.writeShadowDescriptors(point, spot, rectArea, directional, *shadowSampler, linearSampler);
    }
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
    TextureBinding irradianceBinding{BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
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
    passes.reset();
    pickingReadback = {};
    lightBuffer     = {};
    renderStorage   = {};
    materialTable   = {};
    for (FrameContext& frame : frames)
    {
        frame.reset();
    }
    for (auto& data : frameStorage)
        data.reset();
    shadowSampler = nullptr;
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
    appliedConfig.reset();
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
    DCHECK(passes);
    const auto& config   = *rendererConfig;
    activeFramesInFlight = config.renderPath == RenderPath::OfflinePathTracing ? 1u : maxFramesInFlight;
    frameIndex           = 0;

    materialTable.setup(graph);
    lightBuffer        = {};
    const auto  target = graph.importSwapchain();
    SceneOutput scene;
    switch (config.renderPath)
    {
        case RenderPath::OfflinePathTracing:
            scene = setupOffline();
            break;
        case RenderPath::RayTracing:
            scene = setupRayTracing();
            break;
        case RenderPath::Forward:
        case RenderPath::Deferred:
            scene = setupRasterization(target);
            break;
    }
    scene       = setupLightMarkers(scene);
    scene.color = setupPostProcess(scene.color, target);
    setupEditor(scene);
    graph.compile({vulkan, swapchain.extent(), frameStorage, config});
    if (config.renderPath == RenderPath::Forward || config.renderPath == RenderPath::Deferred)
    {
        createShadowSampler();
        writeShadowDescriptors(scene.shadow);
        for (auto& data : frameStorage)
            data.writeLightBufferDescriptor(graph.getBuffer(lightBuffer, 0), graph.bufferRevision(lightBuffer));
    }
    appliedConfig = config;
}

Renderer::SceneOutput Renderer::setupRayTracing()
{
    const auto blas       = setupPass<BlasBuildPass>();
    const auto structures = setupPass<TlasBuildPass>(blas);
    const auto traced     = setupPass<RayTracingPass>(structures);
    auto       color      = traced.rayImage;
    if (rendererConfig->feature.rayTracing.svgf)
        color = setupPass<TemporalReprojectionPass>(traced).color;
    return {.color = color};
}

Renderer::SceneOutput Renderer::setupOffline()
{
    const auto blas       = setupPass<BlasBuildPass>();
    const auto structures = setupPass<TlasBuildPass>(blas);
    return {.color = setupPass<OfflinePathTracingPass>(structures).rayImage};
}

uint32_t Renderer::offlineSampleCount() const
{
    return passes ? passes->get<OfflinePathTracingPass>().sampleCount() : 0;
}

Renderer::SceneOutput Renderer::setupRasterization(RenderGraph::ImageHandle target)
{
    lightBuffer               = graph.createBuffer({
                      .size  = sizeof(LightData),
                      .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    const auto&        config = *rendererConfig;
    ShadowPass::Output shadow;
    if (config.feature.shadow.enable)
        shadow = setupPass<ShadowPass>(lightBuffer);
    const auto depth = setupPass<DepthPrepass>().depth;

    SceneOutput output;
    if (config.renderPath == RenderPath::Deferred)
    {
        const auto gBuffer  = setupPass<GBufferPass>(depth);
        const auto lighting = setupPass<DeferredLightingPass>(target, gBuffer, shadow, lightBuffer);
        output              = {lighting.color, lighting.depth};
        shadow              = lighting.shadow;
    }
    else
    {
        const auto scene = setupPass<ScenePass>(target, depth, shadow, lightBuffer);
        output           = {scene.color, scene.depth};
        shadow           = scene.shadow;
    }
    if (config.feature.ibl)
        output.color = setupPass<SkyboxPass>(output.color, output.depth).color;
    output.color  = setupPass<TransparentPass>(output.color, output.depth, shadow, lightBuffer).color;
    output.shadow = shadow;
    return output;
}

Renderer::SceneOutput Renderer::setupLightMarkers(SceneOutput scene)
{
    if (scene.depth.id == RenderGraph::kInvalidHandle)
        scene.depth = setupPass<EditorDepthPass>().depth;

    const auto markers = setupPass<LightMarkerPass>(scene.color, scene.depth);
    return {markers.color, markers.depth, scene.shadow};
}

RenderGraph::ImageHandle Renderer::setupPostProcess(RenderGraph::ImageHandle color, RenderGraph::ImageHandle target)
{
    const auto& config = *rendererConfig;
    if (config.feature.hdr)
    {
        CHECK(swapchain.surfaceFormat().colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
        if (config.feature.bloom.enable)
            color = setupPass<BloomPass>(color).color;
        return setupPass<ToneMappingPass>(color, target).color;
    }
    if (config.renderPath == RenderPath::RayTracing || config.renderPath == RenderPath::OfflinePathTracing)
    {
        const auto capabilities =
            vkCheck(vulkan.physicalDeviceHandle().getSurfaceCapabilitiesKHR(vulkan.surfaceHandle()));
        CHECK(
            capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst,
            "ray tracing requires transfer-destination swapchain images"
        );
        return setupPass<RayTracingBlitPass>(color, target).color;
    }
    return color;
}

void Renderer::setupEditor(SceneOutput scene)
{
    const auto [pickingImage] = setupPass<EditorPickingPass>(scene.depth);
    const auto [readback]     = setupPass<EditorPickingCopyPass>(pickingImage);
    const auto [finalColor]   = setupPass<EditorUiPass>(scene.color);
    setupPass<EndPass>(finalColor, readback);
    pickingReadback = readback;
}

void Renderer::applyConfiguration()
{
    const auto& config   = *rendererConfig;
    const auto  graphKey = [](const RendererConfig& value)
    {
        const auto& shadow = value.feature.shadow;
        return std::tuple{
            value.renderPath,
            value.feature.ibl,
            value.feature.hdr,
            value.feature.bloom.enable,
            value.feature.bloom.levels,
            value.feature.rayTracing.maxRecursionDepth,
            value.feature.rayTracing.svgf,
            value.feature.offline.maxRecursionDepth,
            shadow.enable,
            shadow.pointShadowMapSize,
            shadow.rectAreaShadowMapSize,
            shadow.spotShadowMapSize,
            shadow.cascade.shadowMapSize,
            shadow.cascade.count
        };
    };
    if (!appliedConfig || graphKey(*appliedConfig) != graphKey(config))
    {
        rebuildGraph();
        return;
    }
    if (appliedConfig->feature.shadow.filter != config.feature.shadow.filter)
    {
        waitIdle();
        if (config.renderPath == RenderPath::Forward || config.renderPath == RenderPath::Deferred)
        {
            createShadowSampler();
            for (auto& data : frameStorage)
                data.writeShadowSampler(*shadowSampler);
        }
    }
    appliedConfig = config;
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
    auto&       assets        = *context().assetManager;
    auto*       profiler      = context().profiler;
    auto&       renderData    = renderStorage[targetFrameIndex];
    renderData.drawItems      = std::span{scene.drawItems};
    renderData.environmentMap = scene.environmentMap;
    renderData.materialIndices.resize(renderData.drawItems.size());
    {
        CpuProfileScope timing{profiler, "Render resources"};
        for (uint32_t itemIndex = 0; itemIndex < renderData.drawItems.size(); ++itemIndex)
        {
            const auto& item = renderData.drawItems[itemIndex];
            // mesh数据读进GPU
            resources.mesh(item.mesh);

            // 记录material在SSBO的idx
            auto                       material = assets.get<MaterialAsset>(item.materialId).as<PbrMaterialAsset>();
            MaterialTable::MaterialKey materialKey{.assetId = item.materialId};
            if (item.materialOverride)
            {
                applyOptionalFields(material, *item.materialOverride);
                materialKey.overrideOwner = {item.selectionId, item.instanceIndex, item.submeshIndex};
            }

            renderData.materialIndices[itemIndex] = materialTable.update(materialKey, material, resources, assets);
        }

        // 如果有新纹理或SSBO需要扩容，则更新material descriptor set
        materialTable.upload(vulkan, targetFrameIndex);
        renderData.materialDescriptorSet = materialTable.descriptorSetHandle(targetFrameIndex);
    }

    renderData.lightRanges = groupLights(scene.lights, renderData.lights);
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

    if (rasterization)
        updateEnvironmentBindings(renderData.environmentMap);

    {
        CpuProfileScope timing{context().profiler, "Frame upload"};
        auto&           data = frameStorage[targetFrameIndex];
        data.uploadSceneUniforms(view.uniforms);
        if (rasterization)
        {
            const auto&         feature = config.feature;
            const auto&         shadow  = feature.shadow;
            const LightUniforms lighting{
                .lightRanges                   = renderData.lightRanges,
                .ambientCoefficient            = feature.ambientCoefficient,
                .cascadeCount                  = shadow.cascade.count,
                .directionalShadowSampleRadius = shadow.cascade.sampleRadius,
                .iblParameters                 = {prefilteredMaxLod, 0.0f, 0.0f, 0.0f},
                .shadowParameters =
                    {shadow.bias, shadow.sampleRadius, shadow.pixelRotation ? 1.0f : 0.0f, shadow.spotSampleRadius},
            };
            data.uploadLightUniforms(lighting);

            if (!renderData.lights.empty())
            {
                const auto upload = graph.allocateUpload(lightBuffer, renderData.lights.size() * sizeof(LightData));
                auto*      destination = reinterpret_cast<LightData*>(upload.data.data());
                for (auto light : renderData.lights)
                {
                    light.shadowEnabled = 0;
                    light.shadowidx     = ~0u;
                    std::construct_at(destination++, light);
                }
            }
            if (data.lightBufferRevision() != graph.bufferRevision(lightBuffer))
                data.writeLightBufferDescriptor(graph.getBuffer(lightBuffer, 0), graph.bufferRevision(lightBuffer));
        }
    }
    return renderData;
}

RenderFrameResult Renderer::render(const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor)
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

    const auto&     renderData = prepare(view, scene, frameIndex);
    const auto      renderPath = rendererConfig->renderPath;
    auto&           data       = frameStorage[frameIndex];
    const PassSetup setup{vulkan, swapchain.extent(), frameStorage, *rendererConfig};
    const PassFrame execution{setup, data, view, renderData, editor, frameIndex};
    graph.prepare(execution);

    {
        CpuProfileScope timing{context().profiler, "Command recording"};
        auto&           commandBuffer = frame.commandBufferHandle();
        vkCheck(commandBuffer.reset());
        vkCheck(commandBuffer.begin({}));
        graph.execute(commandBuffer, execution, imageIndex, frame.gpuProfiler(), context().profiler);
        vkCheck(commandBuffer.end());
    }

    const bool directBlit = (renderPath == RenderPath::RayTracing || renderPath == RenderPath::OfflinePathTracing) &&
        !rendererConfig->feature.hdr;
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
