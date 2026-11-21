#include "passes/raytracing/realtime/RayTracingPass.h"

#include "editor/PassEditor.h"
#include "render/RenderConfig.h"

namespace
{
bool drawRestirSettings(Gui::Widgets& widgets, RestirSettings& restir)
{
    bool edited = false;
    if (widgets.resetHeader("ReSTIR"))
    {
        restir = {};
        edited = true;
    }

    edited |= widgets.slider("Light candidates (M)", restir.candidateCount, 1u, kMaxRestirCandidateCount);
    widgets.tooltip(
        "Light candidates plus one BRDF candidate per surface. Primary direct lighting uses ReSTIR DI."
    );

    edited |= widgets.checkbox("Temporal reuse", restir.temporalReuse);
    edited |= widgets.checkbox("Spatial reuse", restir.spatialReuse);
    edited |= widgets.slider("Reservoir history length", restir.maxHistoryLength, 1u, kMaxRestirHistoryLength);
    edited |= widgets.slider("Spatial neighbors", restir.spatialSampleCount, 1u, kMaxRestirSpatialSamples);
    edited |= widgets.slider("Spatial radius", restir.spatialRadius, 1.0f, kMaxRestirSpatialRadius, "%.1f px", true);
    return edited;
}

bool drawSettings(Gui::Widgets& widgets, RayTracingSettings& rayTracing, const RayTracingPass&, const PassEditorContext&)
{
    bool edited = widgets.slider("Max bounces", rayTracing.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    edited |= drawRestirSettings(widgets, rayTracing.restir);
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RayTracingPass>("RayTracingPass", makePassEditor<RayTracingPass, drawSettings>("ReSTIR"));
}
