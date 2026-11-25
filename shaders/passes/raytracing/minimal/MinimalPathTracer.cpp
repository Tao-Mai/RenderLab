#include "MinimalPathTracerSettings.h"

#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Serializer.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingParameters.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/raytracing/RayTracingProgram.h"
#include "render/resources/ShaderLayout.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace
{
constexpr char       kShaderFile[]           = "shaders/passes/raytracing/minimal/minimal_path_tracer.slang";
constexpr char       kOutputColor[]          = "color";
constexpr char       kOutputImage[]          = "outputImage";
constexpr auto       kOutputFormat           = rhi::Format::RGBA32Float;
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

const RenderPassReflection::Field kPorts[]{
    {
        .name = kOutputColor,
        .resource = rhi::TextureDesc{.format = kOutputFormat, .usage = rhi::TextureUsage::UnorderedAccess},
        .visibility = RenderPassReflection::Visibility::Output,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
};

void validateShaderLayout(rhi::IShaderProgram& program)
{
    const ShaderLayout layout(program);
    layout.validateConstants<PathTracingParameters>("parameters");
    CHECK(
        layout.storageImageFormat(kOutputImage) == kOutputFormat,
        "storage image format differs for '{}'",
        kOutputImage
    );
}

class MinimalPathTracer : public RenderPass
{
public:
    MinimalPathTracer(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
        RenderPass(std::move(device)), parameters(DeserializeWithDefaults<MinimalPathTracerSettings>(properties))
    {
        parameters.validate();
    }

    static std::shared_ptr<MinimalPathTracer> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<MinimalPathTracer>(std::move(device), properties);
    }

    void compile(RenderContext*, const CompileData&) override
    {
        if (!checkRayTracingSupport(*mpDevice))
            return;

        rhi::FormatSupport support;
        rhiCheck(mpDevice->getFormatSupport(kOutputFormat, &support));
        if (!rhi::is_set(support, rhi::FormatSupport::ShaderUavStore))
            LOG_ERROR("MinimalPathTracer requires RGBA32Float storage writes");
    }

    void setScene(GpuScene* scene) override
    {
        tracer.reset();
        sampleIndex = 0;
        mpScene     = scene;
        if (!mpScene)
            return;

        auto defines = pathTracingDefines(false);
        defines.emplace("SAMPLE_GENERATOR_TYPE", std::to_string(kUniformSampleGenerator));

        // Triangle instances share hit-group offset zero. The ray type selects the scatter or visibility record.
        RayTracingProgram::Desc desc{
            .source = kShaderFile,
            .entries = {kRaygenEntries[0],
                        kMissEntries[0],
                        kMissEntries[1],
                        kHitGroups[0].closestHitEntryPoint,
                        kHitGroups[0].anyHitEntryPoint,
                        kHitGroups[1].anyHitEntryPoint},
            .shaderIncludes = mpScene->materials().rayTracingShaderIncludes(),
            .pipeline = {
                .hitGroupCount           = uint32_t(kHitGroups.size()),
                .hitGroups               = kHitGroups.data(),
                .maxRecursion            = kMaxRayRecursion,
                .maxRayPayloadSize       = kMaxRayPayloadSize,
                .maxAttributeSizeInBytes = SceneAccelerationStructures::kTriangleAttributeSize,
            },
            .shaderTable = {
                .rayGenShaderCount           = uint32_t(kRaygenEntries.size()),
                .rayGenShaderEntryPointNames = kRaygenEntries.data(),
                .missShaderCount             = uint32_t(kMissEntries.size()),
                .missShaderEntryPointNames   = kMissEntries.data(),
                .hitGroupCount               = uint32_t(kHitGroupNames.size()),
                .hitGroupNames               = kHitGroupNames.data(),
            },
            .validateLayout = validateShaderLayout,
        };
        tracer.emplace(*mpDevice, std::move(desc), std::move(defines));
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
            commands.clearTextureFloat(renderData.getTexture(kOutputColor), rhi::kEntireTexture, clearColor);
            return;
        }

        auto& scene = *mpScene;
        scene.prepareEmissiveLights(commands);

        tracer->addDefine("USE_ANALYTIC_LIGHTS", scene.useAnalyticLights() ? "1" : "0");
        tracer->addDefine("USE_EMISSIVE_LIGHTS", scene.useEmissiveLights() ? "1" : "0");
        tracer->addDefine("HAS_ENV", scene.hasEnvironment() ? "1" : "0");

        const auto& version = tracer->getActiveVersion(*context->shaderManager);
        const rhi::ShaderCursor root(version.shaderObject);

        const PathTracingParameters constants{
            .maxBounces = parameters.maxBounces,
            .sampleIndex = sampleIndex,
        };

        rhiCheck(root["parameters"].getDereferenced().setData(constants));
        rhiCheck(root[kOutputImage].setBinding(renderData.getTexture(kOutputColor)));

        const auto dims = renderData.getDefaultTextureDims();
        scene.raytrace(commands, *version.pipeline, *version.shaderTable, *version.shaderObject, {dims.x, dims.y, 1});
        ++sampleIndex;
    }

private:
    MinimalPathTracerSettings          parameters;
    std::optional<RayTracingProgram>   tracer;
    uint32_t                           sampleIndex    = 0;
    bool                               optionsChanged = false;

    RL_PLUGIN_CLASS(
        MinimalPathTracer,
        "MinimalPathTracer",
        .desc = "Minimal path tracer.",
        .ports = kPorts,
        .settings = pass_settings::bind<
            &MinimalPathTracer::parameters,
            nullptr,
            &MinimalPathTracer::optionsChanged,
            pass_settings::Update::Patch
        >()
    );
};

}  // namespace

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, MinimalPathTracer>();
}
