#include "passes/RayTracingPass/RayTracingPass.h"

#include "editor/PassEditorRegistry.h"
#include "editor/SettingsWidgets.h"

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
    bool edited = false;
    edited |= drawEnumCombo("Tracing algorithm", rayTracing.algorithm, "ReSTIR\0MinimalPathTracer\0");
    ImGui::SetItemTooltip(
        "MinimalPathTracer uses ordinary light sampling and BRDF bounces. ReSTIR uses reservoirs for direct lighting."
    );
    edited |= drawMaxBounces(rayTracing.maxBounces);

    if (rayTracing.algorithm == RealtimeAlgorithm::ReSTIR)
        edited |= drawRestirSettings(rayTracing.restir);
    return edited;
}
}

void registerRayTracingPassEditor(PassEditorRegistry& editors)
{
    editors.add<RayTracingPass, drawSettings>("Ray tracing");
}
