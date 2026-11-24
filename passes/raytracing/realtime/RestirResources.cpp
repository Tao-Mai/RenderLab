#include "passes/raytracing/realtime/RestirResources.h"

#include "core/Logger.h"

#include "passes/raytracing/realtime/RayTracingSettings.h"
#include "passes/raytracing/realtime/RestirFrameParameters.h"

#include <slang-rhi.h>
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"

#include <bit>
#include <cstring>
#include <functional>
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


void RestirResources::prepare(
    GpuResources&                cache,
    const Falcor::RenderContext& frame,
    Falcor::uint2                extent,
    const RestirSettings&        settings,
    rhi::IShaderObject*          object
)
{
    const auto&     data     = *frame.scene;
    const auto&     viewport = frame.view->viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool      ibl              = frame.config->feature.ibl && data.environmentMap.has_value();
    const auto      materialRevision = frame.materialTable->revision();
    uint64_t        signature        = std::hash<std::string>{}(data.sceneId.value);
    const auto      combine          = [&signature](uint64_t value)
    {
        constexpr uint64_t kHashGoldenRatio = 0x9e3779b97f4a7c15ull;
        signature ^= value + kHashGoldenRatio + (signature << 6) + (signature >> 2);
    };
    for (uint32_t index = 0; index < data.drawItems.size(); ++index)
    {
        const auto& item = data.drawItems[index];
        combine(std::hash<std::string>{}(item.mesh.value));
        combine(item.submeshIndex);
        combine(item.selectionId);
        combine(data.materialIndices[index]);
        for (uint32_t column = 0; column < 4; ++column)
            for (uint32_t row = 0; row < 4; ++row)
                combine(std::bit_cast<uint32_t>(item.model[column][row]));
    }

    const bool historyValid = hasHistory && signature == previousSceneSignature &&
        materialRevision == previousMaterialRevision && data.lights == previousLights &&
        data.environmentMap == previousEnvironment && ibl == previousIbl;
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
    const auto& view         = frame.view->uniforms.view;
    previousViewProjection   = frame.view->uniforms.viewProjection;
    previousViewport         = currentViewport;
    previousDepthRow         = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousSceneSignature   = signature;
    previousMaterialRevision = materialRevision;
    previousLights           = data.lights;
    previousEnvironment      = data.environmentMap;
    previousIbl              = ibl;

    rhiCheck(root["restirSurfaces"].setBinding(cache.getBuffer(surfaces[writeLayer])));
    rhiCheck(root["restirPreviousSurfaces"].setBinding(cache.getBuffer(surfaces[writeLayer ^ 1u])));
    rhiCheck(root["restirInitialReservoirs"].setBinding(cache.getBuffer(initialReservoirs)));
    rhiCheck(root["restirHistory"].setBinding(cache.getBuffer(reservoirHistory)));
}


void RestirResources::advanceHistory()
{
    writeLayer ^= 1u;
    hasHistory = true;
}
