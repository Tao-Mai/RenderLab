#include "passes/denoise/SVGF/SVGFReprojection.h"

#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/RenderView.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/RenderContext.h"

namespace
{
constexpr uint32_t kInputCount = 4;

struct ReprojectionParameters
{
    glm::mat4 previousViewProjection;
    glm::vec4 previousViewport;
    glm::vec4 previousDepthRow;
    glm::vec4 viewport;
    uint32_t  writeLayer;
    uint32_t  historyValid;
    uint32_t  historyLength;
    float     alpha;
};
static_assert(sizeof(ReprojectionParameters) == 128);

glm::vec4 linearDepthRow(const glm::mat4& view)
{
    // Only -view.z is needed; keep both camera transforms within 128-byte push constants.
    return -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
}
}  // namespace

SVGFReprojection::Output SVGFReprojection::compile(
    const Falcor::RenderContext&                                      context,
    const DenoiserInputs&                                             source,
    const std::array<GpuResources::ImageHandle, kHistoryCount>&       history,
    const std::array<GpuResources::ImageHandle, kLightingLayerCount>& outputs
)
{
    auto& cache   = *context.gpuResources;
    historySource = source.color;
    extent        = cache.imageInfo(source.color).size;
    hasHistory    = false;
    writeLayer    = 0;
    if (!pipeline)
        createPipeline(context);
    const auto& [geometry, lighting, moments, length] = history;
    const Output output{outputs[kDirectLightingLayer], outputs[kIndirectLightingLayer], lighting};
    rhiCheck(context.device->createRootShaderObject(pipeline->getProgram(), shaderObject.writeRef()));
    const std::array names{
        "currentColor",
        "currentDepth",
        "currentNormal",
        "currentAlbedo",
        "historyGeometry",
        "historyLighting",
        "historyMoments",
        "historyLength",
        "outputDirect",
        "outputIndirect"
    };
    const std::array images{
        source.color,
        source.depth,
        source.worldNormal,
        source.albedo,
        geometry,
        lighting,
        moments,
        length,
        output.direct,
        output.indirect
    };
    for (uint32_t index = 0; index < names.size(); ++index)
        rhiCheck(
            rhi::ShaderCursor(shaderObject)[names[index]].setBinding(cache.getImage(images[index])->getDefaultView())
        );
    return output;
}


void SVGFReprojection::createPipeline(const Falcor::RenderContext& context)
{
    pipeline = context.device->createComputePipeline(
        {.program = context.shaderManager->program(context.shaderManager->getOrLoad("svgf_reprojection"))}
    );
    CHECK(pipeline);
}


void SVGFReprojection::execute(GpuResources& cache, rhi::ICommandEncoder& commands, const Falcor::RenderContext& frame)
{

    const auto      historyRevision = cache.imageHistoryRevision(historySource);
    const auto&     viewport        = frame.view->viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const auto      currentDepthRow = linearDepthRow(frame.view->uniforms.view);

    bindFrameData(shaderObject, *frame.frameData);

    {
        const auto uniformValue = ReprojectionParameters{
            .previousViewProjection = previousViewProjection,
            .previousViewport       = previousViewport,
            .previousDepthRow       = previousDepthRow,
            .viewport               = currentViewport,
            .writeLayer             = writeLayer,
            .historyValid           = hasHistory && previousHistoryRevision == historyRevision ? 1u : 0u,
            .historyLength          = settings.historyLength,
            .alpha                  = settings.alpha,
        };
        const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
        CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
        rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
    };
    auto* pass = commands.beginComputePass();
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    const auto groups = dispatchGroups(pipeline, {extent.width, extent.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
    previousViewProjection  = frame.view->uniforms.viewProjection;
    previousViewport        = currentViewport;
    previousDepthRow        = currentDepthRow;
    previousHistoryRevision = historyRevision;
    writeLayer ^= 1u;
    hasHistory = true;
}
