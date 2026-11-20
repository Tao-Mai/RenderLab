#include "passes/StandardPasses.h"

#include "passes/EditorPickingPass/EditorPickingPass.h"
#include "passes/EditorPickingCopyPass/EditorPickingCopyPass.h"
#include "passes/EditorUiPass/EditorUiPass.h"
#include "passes/postprocess/Bloom/Bloom.h"
#include "passes/postprocess/ExposurePass/ExposurePass.h"
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcess.h"
#include "passes/postprocess/PresentColorPass/PresentColorPass.h"
#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "passes/raster/DeferredLightingPass/DeferredLightingPass.h"
#include "passes/raster/DepthPrepass/DepthPrepass.h"
#include "passes/raster/ScenePass/ScenePass.h"
#include "passes/raster/GBufferPass/GBufferPass.h"
#include "passes/raster/ShadowPass/ShadowPass.h"
#include "passes/raster/SkyboxPass/SkyboxPass.h"
#include "passes/raster/TransparentPass/TransparentPass.h"
#include "passes/OfflinePathTracingPass/OfflinePathTracingPass.h"
#include "passes/RayTracingScenePass/RayTracingScenePass.h"
#include "passes/SVGF/SVGF.h"
#include "passes/RayTracingPass/RayTracingPass.h"

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
    passes.add<RayTracingScenePass>();
    passes.add<RayTracingPass>();
    passes.add<OfflinePathTracingPass>();
    passes.add<SVGF>();
    passes.add<ExposurePass>();
    passes.add<Bloom>();
    passes.add<ToneMappingPass>();
    passes.add<FalcorPostProcess>();
    passes.add<PresentColorPass>();
    passes.add<EditorPickingPass>();
    passes.add<EditorPickingCopyPass>();
    passes.add<EditorUiPass>();
    return passes;
}
