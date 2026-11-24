#include "passes/raytracing/bdpt/BdptBindings.h"
#include "render/graph/RenderPassSettings.h"

#include "core/Logger.h"
#include "core/Serializer.h"
#include "passes/raytracing/bdpt/BdptSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/BufferCapacity.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/SceneLightConstants.h"


#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <future>
#include <limits>
#include <numbers>
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
static_assert(sizeof(BdptParameters) == 80);

const RenderPassReflection::Field kPorts[]{
    {
        .name = kOutputColor,
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA16Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Output,
    },
};

class BDPT : public RenderPass
{
public:
    BDPT(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
        RenderPass(std::move(device), 1), parameters(DeserializeWithDefaults<BdptSettings>(properties))
    {
        parameters.validate();
    }

    static std::shared_ptr<BDPT> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<BDPT>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;
    void setScene(GpuScene* scene) override;
    void onSceneUpdates(RenderContext*, SceneUpdateFlags updates) override;

    [[nodiscard]] float sampleProgress() const { return accumulatedProgress; }
    [[nodiscard]] bool  preparingPipeline() const { return !activeProgram || !activeProgram->pipeline; }

private:
    BdptSettings parameters;

    struct PipelineVariant
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable> shaderTable;
        std::future<rhi::ComPtr<rhi::IRayTracingPipeline>> build;
    };
    std::array<std::array<PipelineVariant, kShaderDepthCount>, kPathEnvironmentVariantCount> pipelines;
    PipelineVariant* activeProgram = nullptr;

    BdptSettings   previousSettings;
    glm::vec4      sceneSphere{};
    bool           lightDistributionDirty = true;

    rhi::ComPtr<rhi::IBuffer>          lightDistributionBuffer;
    rhi::ComPtr<rhi::IBuffer>          cameraHistory;
    rhi::ComPtr<rhi::IBuffer>          lightSplats;
    rhi::ComPtr<rhi::IComputePipeline> resolvePipeline;
    rhi::ComPtr<rhi::IComputePipeline> lightDistributionPipeline;

    float    accumulatedProgress = 0;
    uint32_t sampleIndex         = 0;
    uint32_t tileIndex           = 0;
    uint64_t processedPixels     = 0;
    bool     clearHistory        = true;

    void selectPipeline(const RenderContext& context);
    void resetAccumulation();
    void updateScene(const RenderContext& execution);
    void resolve(rhi::ICommandEncoder& commands, rhi::ITexture& output, const PixelBounds& bounds) const;
    void prepareLightDistribution(const RenderContext& execution);

    static bool drawSettings(
        Gui::Widgets& widgets, BdptSettings& settings, const BDPT& pass, const PassEditorContext& context
    );

    RL_PLUGIN_CLASS(
        BDPT,
        "BDPT",
        .desc = "Bidirectional path tracer.",
        .ports = kPorts,
        .settings = pass_settings::bind<&BDPT::parameters, &BDPT::drawSettings>()
    );
};

glm::vec4 computeSceneSphere(const GpuScene& scene)
{
    const auto draws = scene.drawCalls();
    if (draws.empty())
        return {glm::vec3{scene.cameraUniforms().cameraPosition}, kMinimumSceneRadius};

    auto minimum = glm::vec3{std::numeric_limits<float>::max()};
    auto maximum = -minimum;
    for (const auto& draw : draws)
    {
        minimum           = glm::min(minimum, draw.bounds.min);
        maximum           = glm::max(maximum, draw.bounds.max);
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

    const auto extent     = compileData.getTextureDesc(kOutputColor).size;
    const auto pixelCount = size_t(extent.width) * extent.height;

    rhi::BufferDesc desc;
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    desc.size = 2 * sizeof(float);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, lightDistributionBuffer.writeRef()));
    desc.size = pixelCount * sizeof(BdptBinding::AccumulatedPixel);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, cameraHistory.writeRef()));
    desc.size = pixelCount * sizeof(glm::uvec4);
    rhiCheck(mpDevice->createBuffer(desc, nullptr, lightSplats.writeRef()));

    auto& shaders = *context->shaderManager;
    rhiCheck(mpDevice->createComputePipeline(
        {.program = shaders.getProgram("passes/raytracing/bdpt/resolve.slang", {"computeMain"})},
        resolvePipeline.writeRef()
    ));
    rhiCheck(mpDevice->createComputePipeline(
        {.program = shaders.getProgram("passes/raytracing/bdpt/light_distribution.slang", {"computeMain"})},
        lightDistributionPipeline.writeRef()
    ));

    lightDistributionDirty = true;
    activeProgram          = nullptr;
    resetAccumulation();
}


void BDPT::selectPipeline(const RenderContext& context)
{
    const auto depth       = std::bit_ceil(std::max(kMinShaderDepth, parameters.maxBounces));
    const auto depthIndex  = static_cast<size_t>(std::bit_width(depth) - std::bit_width(kMinShaderDepth));
    const bool hasEnvironment = mpScene->hasEnvironment();
    auto&      variant        = pipelines[hasEnvironment][depthIndex];
    if (!variant.pipeline)
    {
        if (!variant.build.valid())
        {
            auto& shaders = *context.shaderManager;
            auto  defines = pathTracingDefines(hasEnvironment);
            defines["BDPT_MAX_DEPTH"] = std::to_string(depth);
            auto* program             = shaders.getProgram(
                "passes/raytracing/bdpt/main.slang",
                {"raygenMain", "missMain", "visibilityMissMain", "closestHitMain"},
                defines
            );
            variant.build = buildPathTracingPipelineAsync(*mpDevice, program);
        }
        if (variant.build.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        {
            variant.pipeline = variant.build.get();
            variant.shaderTable =
                createPathTracingShaderTable(*mpDevice, variant.pipeline->getProgram(), kRaygenEntries);
        }
    }
    activeProgram = &variant;
}


void BDPT::resetAccumulation()
{
    sampleIndex         = 0;
    accumulatedProgress = 0;
    tileIndex           = 0;
    processedPixels     = 0;
    clearHistory        = true;
}


void BDPT::setScene(GpuScene* scene)
{
    RenderPass::setScene(scene);
    resetAccumulation();
    lightDistributionDirty = true;
}


void BDPT::onSceneUpdates(RenderContext*, SceneUpdateFlags updates)
{
    resetAccumulation();
    lightDistributionDirty |= is_set(updates, kSceneShadingUpdates);
    if (!lightDistributionDirty && mpScene->drawCalls().empty() && is_set(updates, SceneUpdateFlags::Camera))
        sceneSphere = computeSceneSphere(*mpScene);
}


void BDPT::updateScene(const RenderContext& execution)
{
    selectPipeline(execution);
    const bool optionsChanged = parameters != previousSettings;
    if (optionsChanged)
        resetAccumulation();

    if (lightDistributionDirty)
        sceneSphere = computeSceneSphere(*mpScene);
    if (lightDistributionDirty || parameters.lightSelection != previousSettings.lightSelection)
    {
        prepareLightDistribution(execution);
        lightDistributionDirty = false;
    }

    previousSettings = parameters;
}


void BDPT::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto& execution = *renderCommands;
    auto&       scene     = *mpScene;
    auto&       commands  = *execution.commands;
    auto*       tlas      = scene.prepareRayTracing(commands);
    scene.prepareEmissiveLights(commands);
    updateScene(execution);

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

    if (activeProgram->pipeline && viewportPixels > 0)
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
            .path        = {.maxBounces = parameters.maxBounces, .sampleIndex = sampleIndex},
            .sceneSphere = sceneSphere,
            .camera      = cameraSampling(scene.cameraUniforms()),
            .tileOffset  = tileOffset,
            .imageSize   = {size.width, size.height},
            .flags       = flags,
        };

        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(activeProgram->pipeline, activeProgram->shaderTable);
        CHECK(object);
        scene.bindRayTracingShaderData(*object, tlas);
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


void BDPT::prepareLightDistribution(const RenderContext& execution)
{
    const auto& scene = *mpScene;
    const auto& emissive = scene.emissiveLights();
    const auto count = static_cast<uint32_t>(scene.analyticLights().size()) + emissive.count() +
        uint32_t(scene.hasEnvironment());
    const auto leaves = std::bit_ceil(std::max(count, 1u));
    ensureBufferCapacity(*mpDevice, lightDistributionBuffer, uint64_t(2) * leaves * sizeof(float));

    const float diskArea = std::numbers::pi_v<float> * sceneSphere.w * sceneSphere.w;
    const auto environment = scene.environmentEmission();
    const float intensity = (scene.environmentMap() ? 1.0f : 0.0f) +
        std::max({environment.r, environment.g, environment.b, 0.0f});
    struct BuildParameters
    {
        uint32_t firstNode, lightCount, writeLeaves, powerWeighted;
        float diskArea, environmentPower;
        uint32_t padding[2]{};
    } build{leaves, count, 1u, uint32_t(parameters.lightSelection == BdptLightSelection::Power),
        diskArea, 4.0f * std::numbers::pi_v<float> * diskArea * intensity};
    auto& commands = *execution.commands;
    for (; build.firstNode != 0; build.firstNode >>= 1, build.writeLeaves = 0)
    {
        auto* pass = commands.beginComputePass();
        CHECK(pass);
        auto* object = pass->bindPipeline(lightDistributionPipeline);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["parameters"].getDereferenced().setData(build));
        scene.bindShaderData(*object);
        rhiCheck(root["lightDistribution"].setBinding(lightDistributionBuffer));
        emissive.bind(root["emissiveLights"]);
        const auto groups = dispatchGroups(lightDistributionPipeline,
            {std::min(build.firstNode, kLightDispatchRowWidth),
             (build.firstNode + kLightDispatchRowWidth - 1) / kLightDispatchRowWidth, 1});
        pass->dispatchCompute(groups.width, groups.height, groups.depth);
        pass->end();
        commands.globalBarrier();
    }
}


bool BDPT::drawSettings(Gui::Widgets& widgets, BdptSettings& settings, const BDPT& pass, const PassEditorContext&)
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


extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, BDPT>();
}
