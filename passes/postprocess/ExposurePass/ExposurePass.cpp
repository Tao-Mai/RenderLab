#include "passes/postprocess/ExposurePass/ExposurePass.h"

#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"

#include <array>
#include <glm/vec4.hpp>

namespace
{
constexpr char     kHistogram[]       = "histogram";
constexpr uint32_t kHistogramBinCount = 256;
constexpr uint32_t kMeteringStride    = 2;

struct ExposureParameters
{
    glm::vec4 viewport;
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
static_assert(sizeof(ExposureParameters) == 64);
}  // namespace

ExposurePass::ExposurePass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)),
    parameters(Falcor::deserializeFromProperties<ExposurePassSettings>(properties))
{
    parameters.validate();
}


Falcor::Properties ExposurePass::getProperties() const { return Falcor::serializeToProperties(parameters); }


void ExposurePass::setProperties(const Falcor::Properties& properties)
{
    ExposurePassSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}


Falcor::RenderPassReflection ExposurePass::reflect(const CompileData&)
{
    Falcor::RenderPassReflection reflection;
    reflection.addInput("hdrColor", "HDR scene color").bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput(kExposureStateOutput, "Exposure state")
        .rawBuffer(sizeof(ExposureState))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    reflection.addInternal(kHistogram, "Luminance histogram")
        .rawBuffer(kHistogramBinCount * sizeof(uint32_t))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    return reflection;
}


void ExposurePass::compile(Falcor::RenderContext* context, const CompileData&)
{
    hasHistory        = false;
    previousScene     = {};
    histogramPipeline = context->device->createComputePipeline(
        {.program =
             context->shaderManager->program(context->shaderManager->getOrLoad("exposure", {{"EXPOSURE_STAGE", "0"}}))}
    );
    CHECK(histogramPipeline);
    resolvePipeline = context->device->createComputePipeline(
        {.program =
             context->shaderManager->program(context->shaderManager->getOrLoad("exposure", {{"EXPOSURE_STAGE", "1"}}))}
    );
    CHECK(resolvePipeline);
}


void ExposurePass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    const auto&              frame     = *renderCommands;
    const ExposureConfig&    settings  = frame.scene->exposure ? *frame.scene->exposure : parameters;
    const auto&              automatic = settings.automatic;
    const auto&              viewport  = frame.view->viewport;
    const bool               enabled   = settings.mode == ExposureMode::Automatic;
    const ExposureParameters parameters{
        .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
        .deltaSeconds          = frame.deltaSeconds,
        .manualEv100           = settings.ev100,
        .exposureCompensation  = settings.exposureCompensation,
        .automatic             = enabled ? 1u : 0u,
        .minEv100              = automatic.minEv100,
        .maxEv100              = automatic.maxEv100,
        .brightAdaptationSpeed = automatic.brightAdaptationSpeed,
        .darkAdaptationSpeed   = automatic.darkAdaptationSpeed,
        .lowPercentile         = automatic.lowPercentile,
        .highPercentile        = automatic.highPercentile,
        .historyValid          = hasHistory && previousScene == frame.scene->sceneId ? 1u : 0u,
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
        rhiCheck(root["hdrColor"].setBinding(renderData.getTexture("hdrColor")->getDefaultView()));
        rhiCheck(root["histogram"].setBinding(histogram));
        rhiCheck(root["exposureState"].setBinding(renderData.getBuffer(kExposureStateOutput)));
        rhiCheck(root["parameters"].getDereferenced().setData(parameters));
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
    hasHistory    = true;
    previousScene = frame.scene->sceneId;
}
