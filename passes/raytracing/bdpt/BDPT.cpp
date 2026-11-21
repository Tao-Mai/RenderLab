#include "passes/raytracing/bdpt/BdptBindings.h"
#include "passes/raytracing/bdpt/BdptSettings.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/offline/OfflinePathTracing.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/offline/OfflineTracePipelineCache.h"

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
constexpr uint32_t kMinShaderDepth = 4;
constexpr uint32_t kTileSize = 128;
constexpr uint32_t kMinTileSize = 8;
constexpr uint32_t kTileReferenceDepth = 6;
constexpr float kMinimumSceneRadius = 0.001f;
constexpr float kSceneBoundsPadding = 1.001f;

constexpr std::array kExtraBindings{
    vk::DescriptorSetLayoutBinding{
        .binding = BdptBinding::kSplats,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute,
    },
    vk::DescriptorSetLayoutBinding{
        .binding = BdptBinding::kLightDistribution,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR,
    },
};

struct alignas(16) BdptParameters
{
    OfflinePathParameters path;
    glm::vec4 sceneSphere;
    glm::vec4 camera;
    glm::uvec2 tileOffset;
    float lightSamples;
    uint32_t flags;
};
static_assert(sizeof(BdptParameters) == 112);

struct LightDistributionEntry
{
    uint32_t alias = 0;
    float acceptance = 1.0f;
    float probability = 0;
    uint32_t padding = 0;
};
static_assert(sizeof(LightDistributionEntry) == 16);

class BDPT : public ConfigurablePass<BdptSettings>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup&) const override
    {
        RenderPassReflection reflection;
        reflection.output("rayImage", PortKind::Image);
        return reflection;
    }

    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void bindResources(RenderGraph& graph, const PassSetup& setup) override;
    void prepare(RenderGraph& graph, const PassFrame& execution) override;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& execution) override;

    [[nodiscard]] nlohmann::json statistics() const override
    {
        return {{"sampleCount", tracer.sampleCount()}, {"sampleProgress", sampleProgress()},
                {"preparingPipeline", preparingPipeline()}};
    }

    [[nodiscard]] float sampleProgress() const { return accumulatedProgress; }
    [[nodiscard]] bool preparingPipeline() const { return pipeline == nullptr; }

private:
    OfflinePathTracing tracer;
    RenderGraph::BufferHandle splats;
    RenderGraph::BufferHandle lightDistribution;
    uint64_t lightDistributionRevision = 0;

    const RayTracingPipeline* pipeline = nullptr;
    uint32_t shaderDepth = 0;
    std::string shaderPath;
    glm::vec4 sceneSphere{};
    BdptSettings previousSettings;
    float accumulatedProgress = 0;
    uint32_t tileIndex = 0;
    uint64_t processedPixels = 0;

    void selectPipeline(const VulkanContext& vulkan);
    void resetAccumulation();
    void prepareLightDistribution(RenderGraph& graph, const PassFrame& execution);
};

glm::vec4 computeSceneSphere(const PassFrame& execution)
{
    const auto& items = execution.renderData.drawItems;
    if (items.empty())
        return {glm::vec3{execution.view.uniforms.cameraPosition}, kMinimumSceneRadius};

    auto minimum = glm::vec3{std::numeric_limits<float>::max()};
    auto maximum = -minimum;
    for (const auto& item : items)
    {
        const auto bounds = context().assetManager->get(item.mesh).bounds.transformed(item.model);
        minimum = glm::min(minimum, bounds.min);
        maximum = glm::max(maximum, bounds.max);
    }
    const auto center = (minimum + maximum) * 0.5f;
    return {center, std::max(glm::length(maximum - center) * kSceneBoundsPadding, kMinimumSceneRadius)};
}

glm::vec4 cameraSampling(const RenderView& view)
{
    const auto& uniforms = view.uniforms;
    const auto forward = glm::normalize(-glm::vec3{uniforms.view[0][2], uniforms.view[1][2], uniforms.view[2][2]});
    const auto onImagePlane = [&](glm::vec2 ndc)
    {
        const auto homogeneous = uniforms.inverseViewProjection * glm::vec4{ndc, 1, 1};
        const auto offset = glm::vec3{homogeneous} / homogeneous.w - glm::vec3{uniforms.cameraPosition};
        return offset / glm::dot(offset, forward);
    };
    const auto corner = onImagePlane({-1, -1});
    return {forward, glm::length(glm::cross(onImagePlane({1, -1}) - corner, onImagePlane({-1, 1}) - corner))};
}

void BDPT::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto image = tracer.compile(graph, setup, "bdpt_resolve", sizeof(BdptParameters), kExtraBindings);
    const auto extent = setup.renderExtent;
    const auto splatBytes = vk::DeviceSize(extent.width) * extent.height * sizeof(glm::uvec4);
    CHECK(splatBytes <= setup.vulkan.properties().limits.maxStorageBufferRange);
    splats = graph.write(graph.createBuffer({
        .size = splatBytes,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    }));
    graph.use(
        splats,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
                vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite,
        }
    );

    lightDistribution = graph.createBuffer({
        .size = sizeof(LightDistributionEntry),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    graph.use(
        lightDistribution,
        {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR, .access = vk::AccessFlagBits2::eShaderStorageRead}
    );
    lightDistributionRevision = 0;
    selectPipeline(setup.vulkan);
    resetAccumulation();
    ports.setOutput("rayImage", image);
}

void BDPT::bindResources(RenderGraph& graph, const PassSetup& setup)
{
    tracer.bindResources(graph, setup);
    tracer.bindStorageBuffer(graph, setup.vulkan, splats, BdptBinding::kSplats);
}

void BDPT::selectPipeline(const VulkanContext& vulkan)
{
    const auto depth = std::bit_ceil(std::max(kMinShaderDepth, parameters.maxBounces));
    if (depth != shaderDepth)
    {
        shaderDepth = depth;
        shaderPath = std::format("shaders/bdpt.depth{}.spv", depth);
        pipeline = nullptr;
    }
    if (!pipeline)
        pipeline = renderContext().offlinePipelines->get(vulkan, tracer.layout(), shaderPath, true);
}

void BDPT::resetAccumulation()
{
    tracer.resetAccumulation();
    accumulatedProgress = 0;
    tileIndex = 0;
    processedPixels = 0;
}

void BDPT::prepare(RenderGraph& graph, const PassFrame& execution)
{
    const bool sceneChanged = tracer.prepareScene(graph, execution);
    selectPipeline(execution.setup.vulkan);
    if (sceneChanged || parameters != previousSettings)
        resetAccumulation();

    if (sceneChanged || lightDistributionRevision == 0)
        sceneSphere = computeSceneSphere(execution);
    if (sceneChanged || lightDistributionRevision == 0 || parameters.lightSelection != previousSettings.lightSelection)
        prepareLightDistribution(graph, execution);

    previousSettings = parameters;
}

void BDPT::execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& execution)
{
    const auto bounds = tracer.viewportBounds(execution.view);
    const uint64_t viewportPixels = uint64_t(bounds.extent.width) * bounds.extent.height;
    const bool trace = pipeline && viewportPixels > 0;
    glm::uvec2 tileOffset{0};
    vk::Extent2D batchExtent{};
    uint32_t tileCount = 0;
    float lightSamples = accumulatedProgress;

    if (trace)
    {
        const auto depthScale = std::bit_ceil((parameters.maxBounces + kTileReferenceDepth - 1) / kTileReferenceDepth);
        const auto tileSize = std::max(kMinTileSize, kTileSize / depthScale);
        const auto columns = (bounds.extent.width + tileSize - 1) / tileSize;
        const auto rows = (bounds.extent.height + tileSize - 1) / tileSize;
        tileCount = columns * rows;

        const auto x = tileIndex % columns * tileSize;
        const auto y = tileIndex / columns * tileSize;
        tileOffset = {uint32_t(bounds.offset.x) + x, uint32_t(bounds.offset.y) + y};
        batchExtent = vk::Extent2D{std::min(tileSize, bounds.extent.width - x), std::min(tileSize, bounds.extent.height - y)};
        lightSamples = float(tracer.sampleCount()) +
            float(processedPixels + uint64_t(batchExtent.width) * batchExtent.height) / float(viewportPixels);
    }

    uint32_t flags = 0;
    if (parameters.lightTracing)
        flags |= BdptBinding::kLightTracing;
    if (parameters.misHeuristic == BdptMisHeuristic::Power)
        flags |= BdptBinding::kPowerHeuristic;

    const BdptParameters constants{
        .path = tracer.pathParameters(execution, parameters.maxBounces),
        .sceneSphere = sceneSphere,
        .camera = cameraSampling(execution.view),
        .tileOffset = tileOffset,
        .lightSamples = lightSamples,
        .flags = flags,
    };
    tracer.render(
        graph, commands, execution, std::as_bytes(std::span{&constants, 1}),
        trace ? pipeline : nullptr, batchExtent, std::array{splats}
    );

    if (trace)
    {
        accumulatedProgress = lightSamples;
        processedPixels += uint64_t(batchExtent.width) * batchExtent.height;
        if (++tileIndex == tileCount)
        {
            tracer.finishSample();
            tileIndex = 0;
            processedPixels = 0;
        }
    }
}

void BDPT::prepareLightDistribution(RenderGraph& graph, const PassFrame& execution)
{
    const auto&      scene    = *execution.renderData.offlineScene;
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
    CHECK(bytes <= execution.setup.vulkan.properties().limits.maxStorageBufferRange);

    const auto upload  = graph.allocateUpload(lightDistribution, bytes);
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

    if (lightDistributionRevision != graph.bufferRevision(lightDistribution))
    {
        tracer.bindStorageBuffer(graph, execution.setup.vulkan, lightDistribution, BdptBinding::kLightDistribution);
        lightDistributionRevision = graph.bufferRevision(lightDistribution);
    }
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
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<BDPT>("BDPT", makePassEditor<BDPT, drawSettings>("BDPT"));
}
