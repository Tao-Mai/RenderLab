#include "render/impl/passes/restir/RestirPathTracer.h"

#include <slang-rhi/shader-cursor.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/resources/ShaderLayout.h"

#include <string>
#include <utility>

namespace
{
constexpr char       kShaderFile[]           = "passes/restir/restir.slang";
constexpr char       kVBuffer[]              = "vbuffer";
constexpr char       kOutputColor[]          = "color";
constexpr char       kOutputImage[]          = "outputImage";
constexpr char       kReservoirHistory[]     = "reservoirHistory";
constexpr auto       kOutputFormat           = rhi::Format::RGBA32Float;
constexpr uint32_t   kMaxRayPayloadSize      = 256;
constexpr uint32_t   kMaxRayRecursion        = 1;
constexpr uint32_t   kUniformSampleGenerator = 1;

struct ReservoirHistory
{
    int32_t selectedLightIndex;
    glm::vec3 selectedContribution;
    float selectedTarget;
    float weightSum;
    uint32_t sampleCount;
};
static_assert(sizeof(ReservoirHistory) == 28);

const RenderPassReflection::Field kPorts[]{
    {
        .name = kVBuffer,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
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

RestirPathTracer::RestirPathTracer(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(std::move(device), {.type = kType, .ports = kPorts})
{
}

RenderPassReflection RestirPathTracer::reflect(const ResourceCache::DefaultProperties& defaults)
{
    auto reflection = RenderPass::reflect(defaults);
    reflection.addInternal(kReservoirHistory, rhi::BufferDesc{
        .size = size_t(defaults.dims.x) * defaults.dims.y * sizeof(ReservoirHistory),
        .elementSize = sizeof(ReservoirHistory),
        .usage = rhi::BufferUsage::UnorderedAccess,
    });

    return reflection;
}

void RestirPathTracer::compile(RenderContext*, const CompileData&)
{
    rhi::FormatSupport support;
    rhiCheck(mpDevice->getFormatSupport(kOutputFormat, &support));
    CHECK(rhi::is_set(support, rhi::FormatSupport::ShaderUavStore),
          "RestirPathTracer requires RGBA32Float storage writes");
    sampleSeed = 0;
}

void RestirPathTracer::setScene(GpuScene* scene)
{
    tracer.reset();
    sampleSeed = 0;
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

void RestirPathTracer::onSceneUpdates(RenderContext*, SceneUpdateFlags) { sampleSeed = 0; }

void RestirPathTracer::execute(RenderContext* context, const RenderData& renderData)
{
    auto& dictionary = renderData.getDictionary();
    const auto refresh = dictionary.find(kRenderPassRefreshFlags);
    if (refresh != dictionary.end() && refresh->second != std::to_underlying(RenderPassRefreshFlags::None))
        sampleSeed = 0;

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
    rhiCheck(uniforms["sampleSeed"].setData(sampleSeed));
    rhiCheck(root[kVBuffer].setBinding(renderData.getTexture(kVBuffer)));
    rhiCheck(root[kOutputImage].setBinding(renderData.getTexture(kOutputColor)));
    rhiCheck(root[kReservoirHistory].setBinding(renderData.getBuffer(kReservoirHistory)));

    const auto dims = renderData.getDefaultTextureDims();
    scene.raytrace(commands, *version.pipeline, *version.shaderTable, *version.shaderObject, {dims.x, dims.y, 1});
    ++sampleSeed;
}
