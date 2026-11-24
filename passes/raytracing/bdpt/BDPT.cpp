#include "passes/raytracing/bdpt/BdptBindings.h"
#include "render/graph/RenderPass.h"

#include "core/Logger.h"
#include "passes/raytracing/bdpt/BdptSettings.h"

#include <slang-rhi.h>
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "editor/PassEditor.h"
#include "render/PreparedScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/offline/OfflinePathTracing.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/offline/OfflineTracePipelineCache.h"

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
#include "test/PathTracingStats.h"
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <limits>

void BdptSettings::validate() const
{
    CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces);
    CHECK(lightSelection == BdptLightSelection::Uniform || lightSelection == BdptLightSelection::Power);
    CHECK(misHeuristic == BdptMisHeuristic::Balance || misHeuristic == BdptMisHeuristic::Power);
}

namespace
{
constexpr char     kOutputColor[]      = "color";
constexpr uint32_t kMinShaderDepth     = 4;
constexpr uint32_t kTileSize           = 128;
constexpr uint32_t kMinTileSize        = 8;
constexpr uint32_t kTileReferenceDepth = 6;
constexpr float    kMinimumSceneRadius = 0.001f;
constexpr float    kSceneBoundsPadding = 1.001f;

struct alignas(16) BdptParameters
{
    OfflinePathParameters path;
    glm::vec4             sceneSphere;
    glm::vec4             camera;
    glm::uvec2            tileOffset;
    float                 lightSamples;
    uint32_t              flags;
};
static_assert(sizeof(BdptParameters) == 112);

struct LightDistributionEntry
{
    uint32_t alias       = 0;
    float    acceptance  = 1.0f;
    float    probability = 0;
    uint32_t padding     = 0;
};
static_assert(sizeof(LightDistributionEntry) == 16);

class BDPT : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(BDPT, "BDPT", "Bidirectional path tracer.");

    BDPT(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<BdptSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Falcor::Properties getProperties() const override
    {
        return Falcor::serializeToProperties(parameters);
    }

    void setProperties(const Falcor::Properties& properties) override
    {
        BdptSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static Falcor::ref<BDPT> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<BDPT>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        reflection.addOutput(kOutputColor, "Path traced radiance")
            .format(rhi::Format::RGBA16Float)
            .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
        return reflection;
    }

    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

    [[nodiscard]] float sampleProgress() const { return accumulatedProgress; }
    [[nodiscard]] bool  preparingPipeline() const { return pipeline == nullptr; }

private:
    BdptSettings parameters;

    OfflinePathTracing tracer;
    bool               lightDistributionReady = false;
    void updateScene(GpuResources& cache, const Falcor::RenderContext& execution, const Falcor::RenderData& renderData);
    rhi::ComPtr<rhi::IBuffer>  splatBuffer;
    rhi::ComPtr<rhi::IBuffer>  lightDistributionBuffer;
    GpuResources::BufferHandle splats;
    GpuResources::BufferHandle lightDistribution;

    const OfflineTracePipelineCache::Result* pipeline    = nullptr;
    uint32_t                                 shaderDepth = 0;
    glm::vec4                                sceneSphere{};
    BdptSettings                             previousSettings;
    float                                    accumulatedProgress = 0;
    uint32_t                                 tileIndex           = 0;
    uint64_t                                 processedPixels     = 0;

    void selectPipeline(const Falcor::RenderContext& context);
    void resetAccumulation();
    void prepareLightDistribution(GpuResources& cache, const Falcor::RenderContext& execution);
};

glm::vec4 computeSceneSphere(const Falcor::RenderContext& execution)
{
    const auto& items = execution.scene->drawItems;
    if (items.empty())
        return {glm::vec3{execution.view->uniforms.cameraPosition}, kMinimumSceneRadius};

    auto minimum = glm::vec3{std::numeric_limits<float>::max()};
    auto maximum = -minimum;
    for (const auto& item : items)
    {
        const auto bounds = context().assetManager->get(item.mesh).bounds.transformed(item.model);
        minimum           = glm::min(minimum, bounds.min);
        maximum           = glm::max(maximum, bounds.max);
    }
    const auto center = (minimum + maximum) * 0.5f;
    return {center, std::max(glm::length(maximum - center) * kSceneBoundsPadding, kMinimumSceneRadius)};
}

glm::vec4 cameraSampling(const RenderView& view)
{
    const auto& uniforms = view.uniforms;
    const auto  forward  = glm::normalize(-glm::vec3{uniforms.view[0][2], uniforms.view[1][2], uniforms.view[2][2]});
    const auto  onImagePlane = [&](glm::vec2 ndc)
    {
        const auto homogeneous = uniforms.inverseViewProjection * glm::vec4{ndc, 1, 1};
        const auto offset      = glm::vec3{homogeneous} / homogeneous.w - glm::vec3{uniforms.cameraPosition};
        return offset / glm::dot(offset, forward);
    };
    const auto corner = onImagePlane({-1, -1});
    return {forward, glm::length(glm::cross(onImagePlane({1, -1}) - corner, onImagePlane({-1, 1}) - corner))};
}


void BDPT::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    auto&      cache  = *passContext->gpuResources;
    const auto extent = compileData.defaultTexDims;
    tracer.compile(*passContext, extent, "bdpt_resolve");


    rhi::BufferDesc desc;
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    desc.size = size_t(extent.x) * extent.y * sizeof(glm::uvec4);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, splatBuffer.writeRef()));
    splats    = cache.registerBuffer(splatBuffer);
    desc.size = sizeof(LightDistributionEntry);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, lightDistributionBuffer.writeRef()));
    lightDistribution = cache.registerBuffer(lightDistributionBuffer);
    tracer.setLightTracingBuffers(splats, lightDistribution);
    lightDistributionReady = false;
    selectPipeline(*passContext);
    resetAccumulation();
}


void BDPT::selectPipeline(const Falcor::RenderContext& context)
{
    const auto depth = std::bit_ceil(std::max(kMinShaderDepth, parameters.maxBounces));
    if (depth != shaderDepth)
    {
        shaderDepth = depth;
        pipeline    = nullptr;
    }
    if (!pipeline)
        pipeline = context.offlinePipelines->get(
            *mpDevice,
            *context.shaderManager,
            context.shaderManager->getOrLoad("bdpt", {{"BDPT_MAX_DEPTH", std::to_string(shaderDepth)}}),
            true
        );
}


void BDPT::resetAccumulation()
{
    tracer.resetAccumulation();
    accumulatedProgress = 0;
    tileIndex           = 0;
    processedPixels     = 0;
}


void BDPT::updateScene(
    GpuResources& cache, const Falcor::RenderContext& execution, const Falcor::RenderData& renderData
)
{
    const bool sceneChanged = tracer.prepareScene(cache, execution, renderData.getTexture(kOutputColor));
    selectPipeline(execution);
    const bool optionsChanged = parameters != previousSettings;
    if (optionsChanged)
    {
        auto&      dictionary = renderData.getDictionary();
        const auto flags = dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
        dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
    }
    if (sceneChanged || optionsChanged)
        resetAccumulation();

    if (sceneChanged || !lightDistributionReady)
        sceneSphere = computeSceneSphere(execution);
    if (sceneChanged || !lightDistributionReady || parameters.lightSelection != previousSettings.lightSelection)
    {
        prepareLightDistribution(cache, execution);
        lightDistributionReady = true;
    }

    previousSettings = parameters;
}


void BDPT::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache     = *renderCommands->gpuResources;
    auto&       commands  = *renderCommands->commands;
    const auto& execution = *renderCommands;

    updateScene(cache, execution, renderData);
    cache.recordUploads(commands);

    const auto     bounds         = tracer.viewportBounds(*execution.view);
    const uint64_t viewportPixels = uint64_t(bounds.extent.width) * bounds.extent.height;
    const bool     trace          = pipeline && viewportPixels > 0;
    glm::uvec2     tileOffset{0};
    Extent2D       batchExtent{};
    uint32_t       tileCount    = 0;
    float          lightSamples = accumulatedProgress;

    if (trace)
    {
        const auto depthScale = std::bit_ceil((parameters.maxBounces + kTileReferenceDepth - 1) / kTileReferenceDepth);
        const auto tileSize   = std::max(kMinTileSize, kTileSize / depthScale);
        const auto columns    = (bounds.extent.width + tileSize - 1) / tileSize;
        const auto rows       = (bounds.extent.height + tileSize - 1) / tileSize;
        tileCount             = columns * rows;

        const auto x = tileIndex % columns * tileSize;
        const auto y = tileIndex / columns * tileSize;
        tileOffset   = {uint32_t(bounds.offset.x) + x, uint32_t(bounds.offset.y) + y};
        batchExtent =
            Extent2D{std::min(tileSize, bounds.extent.width - x), std::min(tileSize, bounds.extent.height - y)};
        lightSamples = float(tracer.sampleCount()) +
            float(processedPixels + uint64_t(batchExtent.width) * batchExtent.height) / float(viewportPixels);
    }

    uint32_t flags = 0;
    if (parameters.lightTracing)
        flags |= BdptBinding::kLightTracing;
    if (parameters.misHeuristic == BdptMisHeuristic::Power)
        flags |= BdptBinding::kPowerHeuristic;

    const BdptParameters constants{
        .path         = tracer.pathParameters(execution, parameters.maxBounces),
        .sceneSphere  = sceneSphere,
        .camera       = cameraSampling(*execution.view),
        .tileOffset   = tileOffset,
        .lightSamples = lightSamples,
        .flags        = flags,
    };
    tracer.render(
        cache,
        commands,
        execution,
        std::as_bytes(std::span{&constants, 1}),
        trace ? pipeline : nullptr,
        batchExtent,
        std::array{splats}
    );

    if (trace)
    {
        accumulatedProgress = lightSamples;
        processedPixels += uint64_t(batchExtent.width) * batchExtent.height;
        if (++tileIndex == tileCount)
        {
            tracer.finishSample();
            tileIndex       = 0;
            processedPixels = 0;
        }
    }

#ifdef RENDERLAB_TEST_PATH_TRACING_STATS
    renderData.getDictionary()[path_tracing_test::statsKey(getName())] =
        path_tracing_test::OfflineProgress{tracer.sampleCount(), sampleProgress(), preparingPipeline()};
#endif
}


void BDPT::prepareLightDistribution(GpuResources& cache, const Falcor::RenderContext& execution)
{
    const auto&      scene    = *execution.scene->offlineScene;
    const bool       power    = parameters.lightSelection == BdptLightSelection::Power;
    constexpr double kPi      = 3.14159265358979323846;
    const double     diskArea = kPi * double(sceneSphere.w) * sceneSphere.w;

    const auto intensity = [](const OfflineLightEmission& emission)
    {
        const double value = (emission.resources.w & kOfflineLightHasImage) != 0
            ? 1.0
            : std::max({emission.colorScale.r, emission.colorScale.g, emission.colorScale.b});
        return std::max(0.0, value * emission.colorScale.w);
    };

    std::vector<double> weights;
    const auto          append = [&](double weight) { weights.push_back(power ? weight : 1.0); };
    for (const auto& light : scene.pointLights)
        append(4.0 * kPi * intensity(light.emission));
    for (const auto& light : scene.spotLights)
        append(
            2.0 * kPi * ((1.0 - light.cosInner) + (light.cosInner - light.cosOuter) / 5.0) * intensity(light.emission)
        );
    for (const auto& light : scene.directionalLights)
        append(diskArea * intensity(light.emission));
    for (const auto& light : scene.lightTriangles)
        append(
            light.p0Area.w * kPi * (light.twoSided ? 2.0 : 1.0) * intensity(scene.areaEmissions[light.emissionIndex])
        );
    if (scene.hasEnvironmentLight)
        append(4.0 * kPi * diskArea * intensity(scene.environmentLight.emission));

    double sum = 0;
    for (const auto weight : weights)
        sum += weight;
    CHECK(std::isfinite(sum));

    const auto count = weights.size();
    const auto bytes = std::max<size_t>(count, 1) * sizeof(LightDistributionEntry);

    const auto upload  = cache.allocateUpload(lightDistribution, bytes);
    auto*      entries = reinterpret_cast<LightDistributionEntry*>(upload.data.data());

    std::vector<uint32_t> small, large;
    small.reserve(count);
    large.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        const double probability = sum > 0.0 ? weights[index] / sum : 0.0;
        std::construct_at(
            entries + index,
            LightDistributionEntry{
                .alias       = index,
                .acceptance  = 1.0f,
                .probability = static_cast<float>(probability),
            }
        );
        weights[index] = probability * count;
        (weights[index] < 1.0 ? small : large).push_back(index);
    }

    while (!small.empty() && !large.empty())
    {
        const auto below = small.back();
        small.pop_back();
        const auto above = large.back();
        large.pop_back();

        entries[below].acceptance = static_cast<float>(weights[below]);
        entries[below].alias      = above;
        weights[above] += weights[below] - 1.0;
        (weights[above] < 1.0 ? small : large).push_back(above);
    }

    if (count == 0)
        std::construct_at(entries);
}


bool drawSettings(Falcor::Gui::Widgets& widgets, BdptSettings& settings, const BDPT& pass, const PassEditorContext&)
{
    widgets.text(std::format("Samples per pixel: {:.2f}", pass.sampleProgress()));
    if (pass.preparingPipeline())
        widgets.text("Preparing renderer...");

    bool edited = widgets.slider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events across the camera and light subpaths.");
    edited |= widgets.checkbox("Light tracing", settings.lightTracing);
    widgets.tooltip("Connect light subpaths to the camera, including specular caustics.");
    edited |= widgets.dropdown("Light selection", settings.lightSelection, {"Uniform", "Power"});
    edited |= widgets.dropdown("MIS heuristic", settings.misHeuristic, {"Balance", "Power (2)"});
    return edited;
}
}  // namespace


void BDPT::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context->passEditor, "BDPT");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, BDPT>();
}
