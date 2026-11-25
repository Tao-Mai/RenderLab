#include "render/impl/passes/MinimalPathTracer.h"

#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/resources/ShaderLayout.h"

#include <string>
#include <utility>

namespace
{
constexpr char       kShaderFile[]           = "render/impl/passes/minimal_path_tracer.slang";
constexpr char       kOutputColor[]          = "color";
constexpr char       kOutputImage[]          = "outputImage";
constexpr auto       kOutputFormat           = rhi::Format::RGBA32Float;
constexpr uint32_t   kMaxRayPayloadSize      = 256;
constexpr uint32_t   kMaxRayRecursion        = 1;
constexpr uint32_t   kUniformSampleGenerator = 1;

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
    CHECK(
        layout.storageImageFormat(kOutputImage) == kOutputFormat,
        "storage image format differs for '{}'",
        kOutputImage
    );
}
}  // namespace

bool MinimalPathTracer::drawSettings(
    Gui::Widgets& widgets,
    MinimalPathTracerSettings& editedSettings,
    const MinimalPathTracer&,
    const PassEditorContext&
)
{
    constexpr uint32_t kMaxBouncesInEditor = 20;

    const bool changed = widgets.slider("Max bounces", editedSettings.maxBounces, 0u, kMaxBouncesInEditor);
    widgets.tooltip("Maximum indirect bounces. 0 gives direct illumination only.");

    return changed;
}

MinimalPathTracer::MinimalPathTracer(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(
        std::move(device),
        {
            .type = kType,
            .ports = kPorts,
            .settings = pass_settings::bind<
                &MinimalPathTracer::settings, &RenderPassOptions::pathTracer,
                &MinimalPathTracer::drawSettings, &MinimalPathTracer::optionsChanged
            >(),
        }
    ),
    settings{}
{
    settings.validate();
}

void MinimalPathTracer::compile(RenderContext*, const CompileData&)
{
    rhi::FormatSupport support;
    rhiCheck(mpDevice->getFormatSupport(kOutputFormat, &support));
    CHECK(rhi::is_set(support, rhi::FormatSupport::ShaderUavStore),
          "MinimalPathTracer requires RGBA32Float storage writes");
}

void MinimalPathTracer::setScene(GpuScene* scene)
{
    tracer.reset();
    sampleIndex = 0;
    mpScene     = scene;
    if (!mpScene)
        return;

    ShaderDefines defines{
        {"HAS_ENV", "0"},
        {"SAMPLE_GENERATOR_TYPE", std::to_string(kUniformSampleGenerator)},
    };

    RayTracingProgram::Desc desc{
        .source                  = kShaderFile,
        .maxRecursion            = kMaxRayRecursion,
        .maxRayPayloadSize       = kMaxRayPayloadSize,
        .maxAttributeSizeInBytes = SceneAccelerationStructures::kTriangleAttributeSize,
        .validateLayout          = validateShaderLayout,
    };
    tracer.emplace(*mpDevice, std::move(desc), std::move(defines));
}

void MinimalPathTracer::execute(RenderContext* context, const RenderData& renderData)
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

    const auto uniforms = root["uniforms"].getDereferenced();
    rhiCheck(uniforms["maxBounces"].setData(settings.maxBounces));
    rhiCheck(uniforms["sampleIndex"].setData(sampleIndex));
    rhiCheck(root[kOutputImage].setBinding(renderData.getTexture(kOutputColor)));

    const auto dims = renderData.getDefaultTextureDims();
    scene.raytrace(commands, *version.pipeline, *version.shaderTable, *version.shaderObject, {dims.x, dims.y, 1});
    ++sampleIndex;
}
