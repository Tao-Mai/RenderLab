#include "passes/StandardPasses.h"

#include "passes/editor/depth/EditorDepthPass.h"
#include "passes/editor/light_marker/LightMarkerPass.h"
#include "passes/editor/picking/EditorPickingPass.h"
#include "passes/editor/picking_copy/EditorPickingCopyPass.h"
#include "passes/editor/ui/EditorUiPass.h"
#include "passes/postprocess/bloom/Bloom.h"
#include "passes/postprocess/exposure/ExposurePass.h"
#include "passes/postprocess/falcor/FalcorPostProcess.h"
#include "passes/postprocess/tone_mapping/ToneMappingPass.h"
#include "passes/raster/deferred/DeferredLightingPass.h"
#include "passes/raster/depth/DepthPrepass.h"
#include "passes/raster/forward/ScenePass.h"
#include "passes/raster/gbuffer/GBufferPass.h"
#include "passes/raster/shadow/ShadowPass.h"
#include "passes/raster/skybox/SkyboxPass.h"
#include "passes/raster/transparent/TransparentPass.h"
#include "passes/raytracing/blas/BlasBuildPass.h"
#include "passes/raytracing/blit/RayTracingBlitPass.h"
#include "passes/raytracing/offline/OfflinePathTracingPass.h"
#include "passes/raytracing/realtime/scene/RayTracingScenePass.h"
#include "passes/raytracing/realtime/svgf/SVGFFilter.h"
#include "passes/raytracing/realtime/trace/RayTracingPass.h"
#include "passes/raytracing/tlas/TlasBuildPass.h"

namespace
{
// Reprojection and filtering share the color history layer.
class SVGF
{
public:
    SVGFFilter::Output setup(RenderGraph& graph, const PassSetup& setup, RayTracingPass::Output traced)
    {
        const auto lighting = graph.addPass(reprojection, setup, traced);
        return filter.setup(graph, setup, traced, lighting, reprojection);
    }

private:
    SVGFReprojection reprojection;
    SVGFFilter filter;
};
}

RenderPassRegistry standardRenderPasses()
{
    RenderPassRegistry passes;
    passes.add<DepthPrepass>();
    passes.add<ShadowPass>();
    passes.add<ScenePass>();
    passes.add<GBufferPass>();
    passes.add<DeferredLightingPass>();
    passes.add<SkyboxPass>();
    passes.add<TransparentPass>();
    passes.add<BlasBuildPass>();
    passes.add<TlasBuildPass>();
    passes.add<RayTracingScenePass>();
    passes.add<RayTracingPass>();
    passes.add<OfflinePathTracingPass>();
    passes.add<RayTracingBlitPass>();
    passes.add<SVGF>();
    passes.add<ExposurePass>();
    passes.add<Bloom>();
    passes.add<ToneMappingPass>();
    passes.add<FalcorPostProcess>();
    passes.add<EditorDepthPass>();
    passes.add<LightMarkerPass>();
    passes.add<EditorPickingPass>();
    passes.add<EditorPickingCopyPass>();
    passes.add<EditorUiPass>();
    return passes;
}
