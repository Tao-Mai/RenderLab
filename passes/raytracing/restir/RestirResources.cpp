#include "passes/raytracing/restir/RestirResources.h"

#include "core/Logger.h"

#include "passes/raytracing/restir/RayTracingSettings.h"
#include "passes/raytracing/restir/RestirFrameParameters.h"

#include <slang-rhi.h>
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/ShaderLayout.h"

#include <slang-rhi/shader-cursor.h>
#include <string>
#include <tuple>

void RestirResources::compile(const std::array<GpuResources::BufferHandle, kBindingCount>& buffers)
{
    const auto& [current, previous, initial, history] = buffers;
    surfaces                                          = {current, previous};
    initialReservoirs                                 = initial;
    reservoirHistory                                  = history;
    writeLayer                                        = 0;
    hasHistory                                        = false;
}


bool RestirResources::prepare(
    GpuResources&                cache,
    const RenderContext& frame,
    glm::uvec2                extent,
    const RestirSettings&        settings,
    rhi::IShaderObject*          object
)
{
    const auto&     data     = *frame.scene;
    const auto&     viewport = frame.view->viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool sameEnvironment = data.environmentMap == previousEnvironment;
    const bool sameShading = data.shading->revision == previousShadingRevision;
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
    const auto& view          = frame.view->uniforms.view;
    previousViewProjection    = frame.view->uniforms.viewProjection;
    previousViewport          = currentViewport;
    previousDepthRow          = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousShadingRevision   = data.shading->revision;
    previousEnvironment       = data.environmentMap;

    rhiCheck(root["restirSurfaces"].setBinding(cache.getBuffer(surfaces[writeLayer])));
    rhiCheck(root["restirPreviousSurfaces"].setBinding(cache.getBuffer(surfaces[writeLayer ^ 1u])));
    rhiCheck(root["restirInitialReservoirs"].setBinding(cache.getBuffer(initialReservoirs)));
    rhiCheck(root["restirHistory"].setBinding(cache.getBuffer(reservoirHistory)));
    return hasHistory && !historyValid;
}


void RestirResources::advanceHistory()
{
    writeLayer ^= 1u;
    hasHistory = true;
}
