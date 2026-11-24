#include "passes/raytracing/bdpt/BdptBindings.h"
#include "render/graph/RenderPass.h"

#include "core/Logger.h"
#include "passes/raytracing/bdpt/BdptSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "editor/PassEditor.h"
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/BufferCapacity.h"
#include "render/raytracing/PathSceneState.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/SceneShading.h"


#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <future>
#include <limits>
#include <utility>

void BdptSettings::validate() const
{
    CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces);
    CHECK(lightSelection == BdptLightSelection::Uniform || lightSelection == BdptLightSelection::Power);
    CHECK(misHeuristic == BdptMisHeuristic::Balance || misHeuristic == BdptMisHeuristic::Power);
}

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};
constexpr char       kOutputColor[]      = "color";
constexpr uint32_t   kMinShaderDepth     = 4;
constexpr size_t     kShaderDepthCount   =
    std::bit_width(std::bit_ceil(kMaxPathBounces)) - std::bit_width(kMinShaderDepth) + 1;
constexpr uint32_t   kTileSize           = 128;
constexpr uint32_t   kMinTileSize        = 8;
constexpr uint32_t   kTileReferenceDepth = 6;
constexpr float      kMinimumSceneRadius = 0.001f;
constexpr float      kSceneBoundsPadding = 1.001f;

struct alignas(16) BdptParameters
{
    PathTracingParameters path;
    glm::vec4             sceneSphere;
    glm::vec4             camera;
    glm::uvec2            tileOffset;
    glm::uvec2            imageSize;
    uint32_t              flags;
    uint32_t              padding0;
    uint32_t              padding1;
    uint32_t              padding2;
};
static_assert(sizeof(BdptParameters) == 96);

struct LightDistributionEntry
{
    uint32_t alias       = 0;
    float    acceptance  = 1.0f;
    float    probability = 0;
    uint32_t padding     = 0;
};
static_assert(sizeof(LightDistributionEntry) == 16);

class BDPT : public RenderPass
{
    RL_PLUGIN_CLASS(BDPT, "BDPT", "Bidirectional path tracer.");

    BDPT(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
        RenderPass(std::move(device), 1), parameters(deserializeFromProperties<BdptSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Properties getProperties() const override
    {
        return serializeToProperties(parameters);
    }

    void setProperties(const Properties& properties) override
    {
        BdptSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static ref<BDPT> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<BDPT>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData&) override
    {
        RenderPassReflection reflection;
        reflection.addOutput(kOutputColor, rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA16Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        });
        return reflection;
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

    [[nodiscard]] float sampleProgress() const { return accumulatedProgress; }
    [[nodiscard]] bool  preparingPipeline() const { return pipeline == nullptr; }

private:
    BdptSettings parameters;

    struct PipelineVariant
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        std::future<rhi::ComPtr<rhi::IRayTracingPipeline>> build;
    };
    std::array<std::array<PipelineVariant, kShaderDepthCount>, static_cast<size_t>(PathEnvironment::Count)> pipelines;
    rhi::ComPtr<rhi::IShaderTable> shaderTable;
    rhi::IRayTracingPipeline*     pipeline = nullptr;

    PathSceneState sceneState;
    BdptSettings   previousSettings;
    glm::vec4      sceneSphere{};
    bool           lightDistributionReady = false;

    rhi::ComPtr<rhi::IBuffer>          lightDistributionBuffer;
    rhi::ComPtr<rhi::IBuffer>          cameraHistory;
    rhi::ComPtr<rhi::IBuffer>          lightSplats;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;

    float    accumulatedProgress = 0;
    uint32_t sampleIndex         = 0;
    uint32_t tileIndex           = 0;
    uint64_t processedPixels     = 0;
    bool     clearHistory        = true;

    void selectPipeline(const RenderContext& context);
    void resetAccumulation();
    void updateScene(const RenderContext& execution, const SceneShadingData& shading);
    void resolve(rhi::ICommandEncoder& commands, rhi::ITexture& output, const PixelBounds& bounds) const;
    void prepareLightDistribution(const RenderContext& execution, const SceneShadingData& scene);
};

glm::vec4 computeSceneSphere(const GpuScene& scene, const SceneShadingData& shading)
{
    const auto draws      = scene.drawCalls();
    const auto rectangles = std::span{shading.lightTriangles}.subspan(shading.firstRectTriangle);
    if (draws.empty() && rectangles.empty())
        return {glm::vec3{scene.cameraUniforms().cameraPosition}, kMinimumSceneRadius};

    auto minimum = glm::vec3{std::numeric_limits<float>::max()};
    auto maximum = -minimum;
    for (const auto& draw : draws)
    {
        minimum           = glm::min(minimum, draw.bounds.min);
        maximum           = glm::max(maximum, draw.bounds.max);
    }
    for (const auto& triangle : rectangles)
    {
        for (const auto vertex : {glm::vec3{triangle.p0Area}, glm::vec3{triangle.p1}, glm::vec3{triangle.p2}})
        {
            minimum = glm::min(minimum, vertex);
            maximum = glm::max(maximum, vertex);
        }
    }
    const auto center = (minimum + maximum) * 0.5f;
    return {center, std::max(glm::length(maximum - center) * kSceneBoundsPadding, kMinimumSceneRadius)};
}

glm::vec4 cameraSampling(const SceneUniforms& uniforms)
{
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


void BDPT::compile(RenderContext* context, const CompileData& compileData)
{
    if (!checkRayTracingSupport(*mpDevice))
        return;

    const auto extent     = compileData.defaultTexDims;
    const auto pixelCount = size_t(extent.x) * extent.y;

    rhi::BufferDesc desc;
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    desc.size = sizeof(LightDistributionEntry);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, lightDistributionBuffer.writeRef()));
    desc.size = pixelCount * sizeof(BdptBinding::AccumulatedPixel);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, cameraHistory.writeRef()));
    desc.size = pixelCount * sizeof(glm::uvec4);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, lightSplats.writeRef()));

    auto& shaders = *context->shaderManager;
    rhiCheck(mpDevice->createComputePipeline(
        {.program = shaders.getOrLoad("bdpt_resolve")}, resolvePipeline.writeRef()
    ));

    lightDistributionReady = false;
    pipeline               = nullptr;
    resetAccumulation();
}


void BDPT::selectPipeline(const RenderContext& context)
{
    const auto depth       = std::bit_ceil(std::max(kMinShaderDepth, parameters.maxBounces));
    const auto depthIndex  = static_cast<size_t>(std::bit_width(depth) - std::bit_width(kMinShaderDepth));
    const auto environment = pathEnvironment(*context.scene);
    auto&      variant     = pipelines[static_cast<size_t>(environment)][depthIndex];
    if (!variant.pipeline)
    {
        if (!variant.build.valid())
        {
            auto& shaders = *context.shaderManager;
            auto  defines = pathEnvironmentDefines(environment);
            defines["BDPT_MAX_DEPTH"] = std::to_string(depth);
            auto* program = shaders.getOrCompile("bdpt", defines);
            variant.build = shaders.createRayTracingPipelineAsync(pathTracingPipelineDesc(program));
        }
        if (variant.build.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        {
            variant.pipeline = variant.build.get();
            if (!shaderTable)
                shaderTable = createPathTracingShaderTable(*mpDevice, variant.pipeline->getProgram(), kRaygenEntries);
        }
    }
    pipeline = variant.pipeline.get();
}


void BDPT::resetAccumulation()
{
    sampleIndex         = 0;
    accumulatedProgress = 0;
    tileIndex           = 0;
    processedPixels     = 0;
    clearHistory        = true;
}


void BDPT::updateScene(const RenderContext& execution, const SceneShadingData& shading)
{
    const bool sceneChanged = sceneState.update(*execution.scene);
    selectPipeline(execution);
    const bool optionsChanged = parameters != previousSettings;
    if (sceneChanged || optionsChanged)
        resetAccumulation();

    if (sceneChanged || !lightDistributionReady)
        sceneSphere = computeSceneSphere(*execution.scene, shading);
    if (sceneChanged || !lightDistributionReady || parameters.lightSelection != previousSettings.lightSelection)
    {
        prepareLightDistribution(execution, shading);
        lightDistributionReady = true;
    }

    previousSettings = parameters;
}


void BDPT::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto& execution = *renderCommands;
    auto&       scene     = *execution.scene;
    auto&       commands  = *execution.commands;
    const auto rayTracing = scene.prepareRayTracing(commands, execution.frameIndex);
    updateScene(execution, rayTracing.shading());

    if (clearHistory)
    {
        commands.clearBuffer(cameraHistory);
        commands.clearBuffer(lightSplats);
        clearHistory = false;
        renderData.invalidateHistory(kOutputColor);
    }

    const auto     output         = renderData.getTexture(kOutputColor);
    const auto&    size           = output->getDesc().size;
    const auto     bounds         = scene.viewport().pixelBounds({size.width, size.height});
    const uint64_t viewportPixels = uint64_t(bounds.extent.width) * bounds.extent.height;

    if (pipeline && viewportPixels > 0)
    {
        CHECK(sampleIndex < std::numeric_limits<uint32_t>::max());
        const auto depthScale = std::bit_ceil((parameters.maxBounces + kTileReferenceDepth - 1) / kTileReferenceDepth);
        const auto tileSize   = std::max(kMinTileSize, kTileSize / depthScale);
        const auto columns    = (bounds.extent.width + tileSize - 1) / tileSize;
        const auto rows       = (bounds.extent.height + tileSize - 1) / tileSize;

        const auto       x = tileIndex % columns * tileSize;
        const auto       y = tileIndex / columns * tileSize;
        const glm::uvec2 tileOffset{bounds.offset.x + x, bounds.offset.y + y};
        const Extent2D   tileExtent{
            std::min(tileSize, bounds.extent.width - x),
            std::min(tileSize, bounds.extent.height - y),
        };

        uint32_t flags = parameters.lightTracing ? BdptBinding::kLightTracing : 0u;
        if (parameters.misHeuristic == BdptMisHeuristic::Power)
            flags |= BdptBinding::kPowerHeuristic;

        const BdptParameters constants{
            .path        = makePathTracingParameters(execution, parameters.maxBounces, sampleIndex),
            .sceneSphere = sceneSphere,
            .camera      = cameraSampling(scene.cameraUniforms()),
            .tileOffset  = tileOffset,
            .imageSize   = {size.width, size.height},
            .flags       = flags,
        };

        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline, shaderTable);
        CHECK(object);
        rayTracing.bind(object);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["parameters"].getDereferenced().setData(constants));
        rhiCheck(root["cameraHistory"].setBinding(cameraHistory));
        rhiCheck(root["splats"].setBinding(lightSplats));
        rhiCheck(root["lightDistribution"].setBinding(lightDistributionBuffer));
        pass->dispatchRays(0, tileExtent.width, tileExtent.height, 1);
        pass->end();

        processedPixels += uint64_t(tileExtent.width) * tileExtent.height;
        accumulatedProgress = float(sampleIndex) + float(processedPixels) / float(viewportPixels);
        if (++tileIndex == columns * rows)
        {
            ++sampleIndex;
            tileIndex       = 0;
            processedPixels = 0;
        }
    }

    resolve(commands, *output, bounds);

}


void BDPT::resolve(rhi::ICommandEncoder& commands, rhi::ITexture& output, const PixelBounds& bounds) const
{
    const BdptBinding::ResolveParameters constants{
        .viewportOffset = {bounds.offset.x, bounds.offset.y},
        .viewportExtent = {bounds.extent.width, bounds.extent.height},
        .lightSplatScale = parameters.lightTracing && accumulatedProgress > 0.0f ? 1.0f / accumulatedProgress : 0.0f,
    };

    auto* pass = commands.beginComputePass();
    CHECK(pass);
    auto* object = pass->bindPipeline(resolvePipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    rhiCheck(root["parameters"].getDereferenced().setData(constants));
    rhiCheck(root["cameraHistory"].setBinding(cameraHistory));
    rhiCheck(root["splats"].setBinding(lightSplats));
    rhiCheck(root["outputColor"].setBinding(&output));

    const auto& size   = output.getDesc().size;
    const auto  groups = dispatchGroups(resolvePipeline, {size.width, size.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}


void BDPT::prepareLightDistribution(const RenderContext& execution, const SceneShadingData& scene)
{
    const bool       power    = parameters.lightSelection == BdptLightSelection::Power;
    constexpr double kPi      = 3.14159265358979323846;
    const double     diskArea = kPi * double(sceneSphere.w) * sceneSphere.w;

    const auto intensity = [](const GpuLightEmission& emission)
    {
        const double value = (emission.resources.w & (kLightHasImage | kLightMultiplyColor)) == kLightHasImage
            ? 1.0
            : std::max({emission.colorScale.r, emission.colorScale.g, emission.colorScale.b});
        return std::max(0.0, value * emission.colorScale.w);
    };

    std::vector<double> weights;
    const auto          append = [&](double weight) { weights.push_back(power ? weight : 1.0); };
    const auto maxChannel = [](glm::vec3 color) { return std::max({color.r, color.g, color.b}); };
    const auto& ranges = scene.lightRanges;
    for (uint32_t index = ranges.directionalEnd; index < ranges.pointEnd; ++index)
        append(4.0 * kPi * maxChannel(scene.lights[index].color));
    for (uint32_t index = ranges.rectAreaEnd; index < ranges.spotEnd; ++index)
    {
        const auto& light = scene.lights[index];
        const auto cosInner = light.areaSizeCone.z;
        const auto cosOuter = light.areaSizeCone.w;
        append(
            2.0 * kPi * ((1.0 - cosInner) + (cosInner - cosOuter) / (light.spotFalloffExponent + 1.0)) *
            maxChannel(light.color)
        );
    }
    for (uint32_t index = 0; index < ranges.directionalEnd; ++index)
        append(diskArea * maxChannel(scene.lights[index].color));
    for (const auto& light : scene.lightTriangles)
        append(
            light.p0Area.w * kPi * (light.twoSided ? 2.0 : 1.0) * intensity(scene.areaEmissions[light.emissionIndex])
        );
    if (pathEnvironment(*execution.scene) != PathEnvironment::None)
    {
        const auto cubeWeight = execution.scene->environmentMap() ? 1.0 : 0.0;
        append(4.0 * kPi * diskArea * (cubeWeight + intensity(scene.environmentLight.emission)));
    }

    double sum = 0;
    for (const auto weight : weights)
        sum += weight;
    CHECK(std::isfinite(sum));

    const auto count = weights.size();
    std::vector<LightDistributionEntry> entries(std::max<size_t>(count, 1));

    std::vector<uint32_t> small, large;
    small.reserve(count);
    large.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        const double probability = sum > 0.0 ? weights[index] / sum : 0.0;
        entries[index]           = {.alias = index, .probability = static_cast<float>(probability)};
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

    const auto bytes = std::span{entries}.size_bytes();
    ensureBufferCapacity(*mpDevice, lightDistributionBuffer, bytes);
    rhiCheck(execution.commands->uploadBufferData(lightDistributionBuffer, 0, bytes, entries.data()));
}


bool drawSettings(Gui::Widgets& widgets, BdptSettings& settings, const BDPT& pass, const PassEditorContext&)
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


void BDPT::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context, "BDPT");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, BDPT>();
}
