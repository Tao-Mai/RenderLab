#include "passes/raytracing/restir/RestirResources.h"

#include "core/Logger.h"

#include "passes/raytracing/restir/RayTracingSettings.h"
#include "passes/raytracing/restir/RestirFrameParameters.h"

#include <slang-rhi.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingShader.h"

#include <slang-rhi/shader-cursor.h>
#include <string>
#include <tuple>

void RestirResources::compile(const std::array<rhi::IBuffer*, kBindingCount>& buffers)
{
    const auto& [current, previous, initial, history] = buffers;
    surfaces[0]                                       = current;
    surfaces[1]                                       = previous;
    initialReservoirs                                 = initial;
    reservoirHistory                                  = history;
    writeLayer                                        = 0;
    hasHistory                                        = false;
}


bool RestirResources::prepare(
    const RenderContext& frame,
    glm::uvec2                extent,
    const RestirSettings&        settings,
    rhi::IShaderObject*          object
)
{
    const auto&     data     = *frame.scene;
    const auto&     viewport = data.viewport();
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool                  sameEnvironment = data.environmentMap() == previousEnvironment;
    const bool                  sameShading     = data.shadingRevision() == previousShadingRevision;
    const bool historyValid = hasHistory && sameShading && sameEnvironment;
    const RestirFrameParameters frameParameters{
        .previousViewProjection = previousViewProjection,
        .previousViewport       = previousViewport,
        .previousDepthRow       = previousDepthRow,
        .width                  = extent.x,
        .height                 = extent.y,
        .writeLayer             = writeLayer,
        .historyValid           = historyValid ? 1u : 0u,
        .maxHistoryLength       = settings.maxHistoryLength,
        .spatialSampleCount     = settings.spatialSampleCount,
        .spatialRadius          = settings.spatialRadius,
        .temporalReuse          = settings.temporalReuse ? 1u : 0u,
        .spatialReuse           = settings.spatialReuse ? 1u : 0u,
        .candidateCount         = settings.candidateCount,
    };
    const rhi::ShaderCursor root(object);
    rhiCheck(root["restirFrame"].getDereferenced().setData(frameParameters));
    const auto& camera        = data.cameraUniforms();
    const auto& view          = camera.view;
    previousViewProjection    = camera.viewProjection;
    previousViewport          = currentViewport;
    previousDepthRow          = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousShadingRevision   = data.shadingRevision();
    previousEnvironment       = data.environmentMap();

    rhiCheck(root["restirSurfaces"].setBinding(surfaces[writeLayer]));
    rhiCheck(root["restirPreviousSurfaces"].setBinding(surfaces[writeLayer ^ 1u]));
    rhiCheck(root["restirInitialReservoirs"].setBinding(initialReservoirs));
    rhiCheck(root["restirHistory"].setBinding(reservoirHistory));
    return hasHistory && !historyValid;
}


void RestirResources::advanceHistory()
{
    writeLayer ^= 1u;
    hasHistory = true;
}
