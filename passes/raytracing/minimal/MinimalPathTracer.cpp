#include "MinimalPathTracerSettings.h"

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "editor/PassEditor.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <compare>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr char       kShaderFile[]           = "passes/raytracing/minimal/minimal_path_tracer.slang";
constexpr char       kMaxBounces[]           = "maxBounces";
constexpr uint32_t   kMaxRayPayloadSize      = 256;
constexpr uint32_t   kMaxRayRecursion        = 1;
constexpr uint32_t   kUniformSampleGenerator = 1;
constexpr std::array kRaygenEntries{"raygenMain"};
constexpr std::array kMissEntries{"missMain", "visibilityMissMain"};
constexpr std::array kHitGroups{
    rhi::HitGroupDesc{"scatter", "closestHitMain", "scatterAnyHitMain"},
    rhi::HitGroupDesc{"visibility", nullptr, "visibilityAnyHitMain"},
};
constexpr std::array kHitGroupNames{kHitGroups[0].hitGroupName, kHitGroups[1].hitGroupName};

struct TextureChannel
{
    const char* name;
    const char* shaderName;
    rhi::Format format;
};

constexpr std::array kInputChannels{TextureChannel{"vbuffer", "vbuffer", rhi::Format::RGBA32Uint}};
constexpr std::array kOutputChannels{TextureChannel{"color", "outputImage", rhi::Format::RGBA32Float}};

MinimalPathTracerSettings parseProperties(const Properties& properties, MinimalPathTracerSettings settings)
{
    for (const auto& [key, value] : properties)
    {
        if (key == kMaxBounces)
            settings.maxBounces = static_cast<uint32_t>(value);
        else
            LOG_WARNING("Unknown property '{}' in MinimalPathTracer properties.", key);
    }
    settings.validate();
    return settings;
}

class MinimalPathTracer : public RenderPass
{
    RL_PLUGIN_CLASS(MinimalPathTracer, "MinimalPathTracer", "Minimal path tracer.");

    MinimalPathTracer(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) : RenderPass(std::move(device))
    {
        parameters = parseProperties(properties, {});
        sampleGeneratorDefines.emplace("SAMPLE_GENERATOR_TYPE", std::to_string(kUniformSampleGenerator));
    }

    [[nodiscard]] Properties getProperties() const override
    {
        Properties properties;
        properties[kMaxBounces] = parameters.maxBounces;
        return properties;
    }

    void setProperties(const Properties& properties) override
    {
        const auto next = parseProperties(properties, parameters);
        optionsChanged |= parameters.maxBounces != next.maxBounces;
        parameters = next;
    }

    static ref<MinimalPathTracer> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<MinimalPathTracer>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override
    {
        const rhi::Extent3D size{compileData.defaultTexDims.x, compileData.defaultTexDims.y, 1};

        RenderPassReflection reflection;
        for (const auto& channel : kInputChannels)
            reflection.addInput(
                channel.name,
                rhi::TextureDesc{
                    .size   = size,
                    .format = channel.format,
                    .usage  = rhi::TextureUsage::ShaderResource,
                }
            );
        for (const auto& channel : kOutputChannels)
            reflection.addOutput(
                channel.name,
                rhi::TextureDesc{
                    .size   = size,
                    .format = channel.format,
                    .usage  = rhi::TextureUsage::UnorderedAccess,
                }
            );
        return reflection;
    }

    void compile(RenderContext*, const CompileData&) override
    {
        if (!checkRayTracingSupport(*mpDevice))
            return;

        rhi::FormatSupport support;
        rhiCheck(mpDevice->getFormatSupport(kOutputChannels[0].format, &support));
        if (!rhi::is_set(support, rhi::FormatSupport::ShaderUavStore))
            LOG_ERROR("MinimalPathTracer requires RGBA32Float storage writes");

        for (auto& [key, program] : tracer.programs)
            program.shaderObject.setNull();
    }

    void setScene(GpuScene* scene) override
    {
        tracer      = {};
        sampleIndex = 0;
        mpScene     = scene;
        if (!mpScene)
            return;

        tracer.shaderIncludes = mpScene->materials().rayTracingShaderIncludes();
        // Triangle instances share hit-group offset zero. The ray type selects the scatter or visibility record.
        tracer.pipelineDesc = {
            .hitGroupCount           = uint32_t(kHitGroups.size()),
            .hitGroups               = kHitGroups.data(),
            .maxRecursion            = kMaxRayRecursion,
            .maxRayPayloadSize       = kMaxRayPayloadSize,
            .maxAttributeSizeInBytes = SceneAccelerationStructures::kTriangleAttributeSize,
        };
        tracer.shaderTableDesc = {
            .rayGenShaderCount           = uint32_t(kRaygenEntries.size()),
            .rayGenShaderEntryPointNames = kRaygenEntries.data(),
            .missShaderCount             = uint32_t(kMissEntries.size()),
            .missShaderEntryPointNames   = kMissEntries.data(),
            .hitGroupCount               = uint32_t(kHitGroupNames.size()),
            .hitGroupNames               = kHitGroupNames.data(),
        };
    }

    void execute(RenderContext* context, const RenderData& renderData) override
    {
        auto& dictionary = renderData.getDictionary();
        if (optionsChanged)
        {
            dictionary[kRenderPassRefreshFlags] |= std::to_underlying(RenderPassRefreshFlags::RenderOptionsChanged);
            optionsChanged = false;
        }

        auto& commands = *context->commands;

        if (!mpScene)
        {
            float clearColor[4]{};
            for (const auto& channel : kOutputChannels)
                commands.clearTextureFloat(renderData.getTexture(channel.name), rhi::kEntireTexture, clearColor);
            return;
        }

        auto& scene = *mpScene;
        scene.prepareEmissiveLights(commands);

        const ProgramKey key{
            .environment       = pathEnvironment(scene),
            .maxBounces        = parameters.maxBounces,
            .useAnalyticLights = !scene.analyticLights().empty(),
            .useEmissiveLights = scene.emissiveLights().count() != 0,
        };
        auto& program = tracer.programs[key];
        if (!program.shaderObject)
            prepareVars(*context->shaderManager, key, program);
        const rhi::ShaderCursor root(program.shaderObject);

        auto       constants     = makePathTracingParameters(scene, parameters.maxBounces, sampleIndex);
        const auto prngDimension = dictionary.find(kRenderPassPrngDimension);
        constants.prngDimension  = prngDimension != dictionary.end() ? prngDimension->second : 0u;
        rhiCheck(root["parameters"].getDereferenced().setData(constants));

        for (const auto& channel : kInputChannels)
            rhiCheck(root[channel.shaderName].setBinding(renderData.getTexture(channel.name)));
        for (const auto& channel : kOutputChannels)
            rhiCheck(root[channel.shaderName].setBinding(renderData.getTexture(channel.name)));

        const auto dims = renderData.getDefaultTextureDims();
        scene.raytrace(commands, *program.pipeline, *program.shaderTable, *program.shaderObject, {dims.x, dims.y, 1});
        ++sampleIndex;
    }

private:
    MinimalPathTracerSettings parameters;
    ShaderDefines             sampleGeneratorDefines;

    struct ProgramKey
    {
        PathEnvironment environment;
        uint32_t        maxBounces;
        bool            useAnalyticLights;
        bool            useEmissiveLights;

        auto operator<=>(const ProgramKey&) const = default;
    };

    struct Program
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable>        shaderTable;
        rhi::ComPtr<rhi::IShaderObject>       shaderObject;
    };
    struct Tracer
    {
        rhi::RayTracingPipelineDesc       pipelineDesc;
        rhi::ShaderTableDesc              shaderTableDesc;
        std::span<const std::string_view> shaderIncludes;
        std::map<ProgramKey, Program>     programs;
    } tracer;

    uint32_t sampleIndex    = 0;
    bool     optionsChanged = false;

    void prepareVars(ShaderManager& shaders, const ProgramKey& key, Program& variant)
    {
        if (!variant.pipeline)
        {
            auto defines = pathTracingDefines(key.environment);
            defines.insert(sampleGeneratorDefines.begin(), sampleGeneratorDefines.end());
            defines["MAX_BOUNCES"]         = std::to_string(key.maxBounces);
            defines["USE_ANALYTIC_LIGHTS"] = key.useAnalyticLights ? "1" : "0";
            defines["USE_EMISSIVE_LIGHTS"] = key.useEmissiveLights ? "1" : "0";
            auto* program                  = shaders.getProgram(
                kShaderFile,
                {kRaygenEntries[0],
                                  kMissEntries[0],
                                  kMissEntries[1],
                                  kHitGroups[0].closestHitEntryPoint,
                                  kHitGroups[0].anyHitEntryPoint,
                                  kHitGroups[1].anyHitEntryPoint},
                defines,
                tracer.shaderIncludes
            );
            const ShaderLayout layout(*program);
            layout.validateConstants<PathTracingParameters>("parameters");
            for (const auto& channel : kOutputChannels)
                CHECK(
                    layout.storageImageFormat(channel.shaderName) == channel.format,
                    "storage image format differs for '{}'",
                    channel.shaderName
                );

            auto pipelineDesc    = tracer.pipelineDesc;
            pipelineDesc.program = program;
            rhiCheck(mpDevice->createRayTracingPipeline(pipelineDesc, variant.pipeline.writeRef()));

            auto shaderTableDesc    = tracer.shaderTableDesc;
            shaderTableDesc.program = program;
            rhiCheck(mpDevice->createShaderTable(shaderTableDesc, variant.shaderTable.writeRef()));
        }

        rhiCheck(mpDevice->createRootShaderObject(variant.pipeline->getProgram(), variant.shaderObject.writeRef()));
    }
};

bool drawSettings(
    Gui::Widgets& widgets, MinimalPathTracerSettings& settings, const MinimalPathTracer&, const PassEditorContext&
)
{
    const bool edited =
        widgets.slider("Max bounces", settings.maxBounces, 0u, MinimalPathTracerSettings::kMaxIndirectBounces);
    widgets.tooltip("Maximum indirect bounces. 0 gives direct illumination only.");
    return edited;
}
}  // namespace

void MinimalPathTracer::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context, "MinimalPathTracer");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, MinimalPathTracer>();
}
