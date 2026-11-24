#include "passes/denoise/SVGF/SVGFReprojection.h"

#include "core/Logger.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/device/RhiCheck.h"
#include "render/resources/ShaderManager.h"

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
    rhi::IDevice&                                          device,
    ShaderManager&                                         shaders,
    const SVGFInputs&                                      source,
    const std::array<rhi::ITexture*, kHistoryCount>&       history,
    const std::array<rhi::ITexture*, kLightingLayerCount>& outputs
)
{
    extent        = source.color->getDesc().size;
    hasHistory    = false;
    writeLayer    = 0;
    if (!pipeline)
        createPipeline(device, shaders);
    const auto& [geometry, lighting, moments, length] = history;
    const Output output{outputs[kDirectLightingLayer], outputs[kIndirectLightingLayer], lighting};
    rhiCheck(device.createRootShaderObject(pipeline->getProgram(), shaderObject.writeRef()));
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


void SVGFReprojection::createPipeline(rhi::IDevice& device, ShaderManager& shaders)
{
    pipeline = device.createComputePipeline(
        {.program = shaders.getProgram("passes/denoise/SVGF/svgf_reprojection.slang", {"computeMain"})}
    );
    CHECK(pipeline);
}


void SVGFReprojection::execute(rhi::ICommandEncoder& commands, const GpuScene& scene, uint64_t historyRevision)
{
    const auto      sceneGeneration = scene.sceneGeneration();
    const bool      historyValid    = hasHistory && previousHistoryRevision == historyRevision &&
        previousSceneGeneration == sceneGeneration;
    const auto&     viewport        = scene.viewport();
    const auto&     camera          = scene.cameraUniforms();
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const auto      currentDepthRow = linearDepthRow(camera.view);

    const rhi::ShaderCursor root(shaderObject);
    scene.bindShaderData(*shaderObject);

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
    auto* pass = commands.beginComputePass();
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
