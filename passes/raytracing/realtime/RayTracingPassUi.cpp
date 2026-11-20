#include "passes/raytracing/realtime/RayTracingPass.h"

#include "editor/PassEditor.h"
#include "editor/SettingsWidgets.h"
#include "render/RenderConfig.h"

namespace
{
bool drawRestirSettings(RestirSettings& restir)
{
    bool edited = false;
    if (drawSettingsHeader("ReSTIR"))
    {
        restir = {};
        edited = true;
    }

    edited |= drawUnsignedSlider(
        "Light candidates (M)",
        restir.candidateCount,
        1,
        kMaxRestirCandidateCount,
        ImGuiSliderFlags_AlwaysClamp
    );
    ImGui::SetItemTooltip(
        "Light candidates plus one BRDF candidate per surface. Primary direct lighting uses ReSTIR DI."
    );

    edited |= ImGui::Checkbox("Temporal reuse", &restir.temporalReuse);
    edited |= ImGui::Checkbox("Spatial reuse", &restir.spatialReuse);
    edited |= drawUnsignedSlider(
        "Reservoir history length",
        restir.maxHistoryLength,
        1,
        kMaxRestirHistoryLength,
        ImGuiSliderFlags_AlwaysClamp
    );
    edited |= drawUnsignedSlider(
        "Spatial neighbors",
        restir.spatialSampleCount,
        1,
        kMaxRestirSpatialSamples,
        ImGuiSliderFlags_AlwaysClamp
    );
    edited |= ImGui::SliderFloat(
        "Spatial radius",
        &restir.spatialRadius,
        1.0f,
        kMaxRestirSpatialRadius,
        "%.1f px",
        ImGuiSliderFlags_AlwaysClamp
    );
    return edited;
}

bool drawSettings(RayTracingSettings& rayTracing, const RayTracingPass&, const PassEditorContext&)
{
    bool edited = drawNumericSlider("Max bounces", rayTracing.maxBounces, kMinPathBounces, kMaxPathBounces);
    ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    edited |= drawRestirSettings(rayTracing.restir);
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RayTracingPass>(makePassEditor<RayTracingPass, drawSettings>("ReSTIR"));
}
