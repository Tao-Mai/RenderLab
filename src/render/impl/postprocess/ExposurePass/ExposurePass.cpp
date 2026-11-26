#include "render/impl/postprocess/ExposurePass/ExposurePass.h"
#include "render/graph/RenderPassSettings.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "render/impl/postprocess/ExposurePass/ExposureState.h"
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"

namespace
{
constexpr char     kExposureStateOutput[] = "state";
constexpr char     kHistogram[]           = "histogram";
constexpr uint32_t kHistogramBinCount     = 256;
constexpr uint32_t kMeteringStride        = 2;

struct ExposureParameters
{
    float     deltaSeconds;
    float     manualEv100;
    float     exposureCompensation;
    uint32_t  automatic;
    float     minEv100;
    float     maxEv100;
    float     brightAdaptationSpeed;
    float     darkAdaptationSpeed;
    float     lowPercentile;
    float     highPercentile;
    uint32_t  historyValid;
    uint32_t  padding = 0;
};
static_assert(sizeof(ExposureParameters) == 48);
}  // namespace

ExposurePass::ExposurePass(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(
        std::move(device),
        {
            .type = kType,
            .ports = kPorts,
            .settings = pass_settings::bind<&ExposurePass::parameters, &RenderPassOptions::exposure, &ExposurePass::drawSettings>(),
        }
    ),
    parameters{}
{
    parameters.validate();
}

const RenderPassReflection::Field ExposurePass::kPorts[]{
    {
        .name = "hdrColor",
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = kExposureStateOutput,
        .resource = rhi::BufferDesc{.size = sizeof(ExposureState), .usage = rhi::BufferUsage::UnorderedAccess},
        .visibility = RenderPassReflection::Visibility::Output,
    },
    {
        .name = kHistogram,
        .resource = rhi::BufferDesc{
            .size = kHistogramBinCount * sizeof(uint32_t), .usage = rhi::BufferUsage::UnorderedAccess
        },
        .visibility = RenderPassReflection::Visibility::Internal,
    },
};


void ExposurePass::compile(RenderContext* context, const CompileData&)
{
    hasHistory        = false;
    histogramPipeline = mpDevice->createComputePipeline(
        {.program = context->shaderManager->getProgram(
             "postprocess/ExposurePass/exposure.slang",
             {"computeMain"},
             {{"EXPOSURE_STAGE", "0"}}
         )}
    );
    CHECK(histogramPipeline);
    resolvePipeline = mpDevice->createComputePipeline(
        {.program = context->shaderManager->getProgram(
             "postprocess/ExposurePass/exposure.slang",
             {"computeMain"},
             {{"EXPOSURE_STAGE", "1"}}
         )}
    );
    CHECK(resolvePipeline);
}


void ExposurePass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto& frame           = *renderCommands;
    const auto& automatic       = parameters.automatic;
    const auto               sceneGeneration = mpScene->sceneGeneration();
    const bool  enabled         = parameters.mode == ExposureMode::Automatic;
    const ExposureParameters uniforms{
        .deltaSeconds          = frame.deltaSeconds,
        .manualEv100           = parameters.ev100,
        .exposureCompensation  = parameters.exposureCompensation,
        .automatic             = enabled ? 1u : 0u,
        .minEv100              = automatic.minEv100,
        .maxEv100              = automatic.maxEv100,
        .brightAdaptationSpeed = automatic.brightAdaptationSpeed,
        .darkAdaptationSpeed   = automatic.darkAdaptationSpeed,
        .lowPercentile         = automatic.lowPercentile,
        .highPercentile        = automatic.highPercentile,
        .historyValid          = hasHistory && previousSceneGeneration == sceneGeneration ? 1u : 0u,
    };


    auto&       commands  = *renderCommands->commands;
    const auto  histogram = renderData.getBuffer(kHistogram);
    const auto& size      = renderData.getTexture("hdrColor")->getDesc().size;
    const auto  dispatch  = [&](rhi::IComputePipeline* pipeline, rhi::Extent3D groups)
    {
        auto* pass = commands.beginComputePass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline);
        CHECK(object);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["hdrColor"].setBinding(renderData.getTexture("hdrColor")));
        rhiCheck(root["histogram"].setBinding(histogram));
        rhiCheck(root["exposureState"].setBinding(renderData.getBuffer(kExposureStateOutput)));
        if (pipeline == resolvePipeline.get())
            rhiCheck(root["parameters"].getDereferenced().setData(uniforms));
        pass->dispatchCompute(groups.width, groups.height, groups.depth);
        pass->end();
    };
    if (enabled)
    {
        commands.clearBuffer(histogram);
        const auto meteringThreads = [](uint32_t pixels) { return (pixels + kMeteringStride - 1) / kMeteringStride; };
        dispatch(
            histogramPipeline,
            dispatchGroups(histogramPipeline, {meteringThreads(size.width), meteringThreads(size.height), 1})
        );
    }
    dispatch(resolvePipeline, {1, 1, 1});
    hasHistory              = true;
    previousSceneGeneration = sceneGeneration;
}
