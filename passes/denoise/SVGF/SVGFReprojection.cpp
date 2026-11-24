#include "passes/denoise/SVGF/SVGFReprojection.h"

#include "core/Logger.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
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
    const RenderContext&                                   context,
    const SVGFInputs&                                      source,
    const std::array<rhi::ITexture*, kHistoryCount>&       history,
    const std::array<rhi::ITexture*, kLightingLayerCount>& outputs
)
{
    extent        = source.color->getDesc().size;
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
    const rhi::ShaderCursor root(shaderObject);
    for (uint32_t index = 0; index < names.size(); ++index)
        rhiCheck(root[names[index]].setBinding(images[index]));
    return output;
}


void SVGFReprojection::createPipeline(const RenderContext& context)
{
    pipeline = context.device->createComputePipeline(
        {.program = context.shaderManager->getOrLoad("svgf_reprojection")}
    );
    CHECK(pipeline);
}


void SVGFReprojection::execute(const RenderContext& frame, uint64_t historyRevision)
{
    const auto      sceneGeneration = frame.scene->sceneGeneration();
    const bool      historyValid    = hasHistory && previousHistoryRevision == historyRevision &&
        previousSceneGeneration == sceneGeneration;
    const auto&     viewport        = frame.scene->viewport();
    const auto&     camera          = frame.scene->cameraUniforms();
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const auto      currentDepthRow = linearDepthRow(camera.view);

    const rhi::ShaderCursor root(shaderObject);
    rhiCheck(root["sceneGlobals"].setBinding(frame.scene->globalsBuffer()));

    {
        const auto uniformValue = ReprojectionParameters{
            .previousViewProjection = previousViewProjection,
            .previousViewport       = previousViewport,
            .previousDepthRow       = previousDepthRow,
            .viewport               = currentViewport,
            .writeLayer             = writeLayer,
            .historyValid           = historyValid ? 1u : 0u,
            .historyLength          = settings.historyLength,
            .alpha                  = settings.alpha,
        };
        const auto uniformCursor = root["parameters"].getDereferenced();
        CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
        rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
    };
    auto* pass = frame.commands->beginComputePass();
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    const auto groups = dispatchGroups(pipeline, {extent.width, extent.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
    previousViewProjection  = camera.viewProjection;
    previousViewport        = currentViewport;
    previousDepthRow        = currentDepthRow;
    previousHistoryRevision = historyRevision;
    previousSceneGeneration = sceneGeneration;
    writeLayer ^= 1u;
    hasHistory = true;
}
