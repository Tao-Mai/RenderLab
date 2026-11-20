#include "passes/OfflinePathTracingPass/OfflinePathTracingPass.h"

#include "editor/PassEditor.h"
#include "editor/SettingsWidgets.h"

namespace
{
bool drawSettings(
    OfflinePathTracingSettings& offline, const OfflinePathTracingPass& pass, const PassEditorContext&
)
{
    bool edited = false;
    ImGui::Text("Samples per pixel: %.2f", pass.sampleProgress());
    if (pass.preparingPipeline())
        ImGui::TextUnformatted("Preparing renderer...");

    edited |= drawEnumCombo("Algorithm", offline.algorithm, "PT\0BDPT\0");
    auto& maxBounces = offline.algorithm == OfflineAlgorithm::PT ? offline.pt.maxBounces : offline.bdpt.maxBounces;
    edited |= drawMaxBounces(maxBounces);

    if (offline.algorithm == OfflineAlgorithm::BDPT)
    {
        auto& bdpt = offline.bdpt;
        edited |= ImGui::Checkbox("Light tracing", &bdpt.lightTracing);
        ImGui::SetItemTooltip("Connect light subpaths to the camera, including specular caustics.");
        edited |= drawEnumCombo("Light selection", bdpt.lightSelection, "Uniform\0Power\0");
        edited |= drawEnumCombo("MIS heuristic", bdpt.misHeuristic, "Balance\0Power (2)\0");
    }

    ImGui::TextUnformatted("Scene, camera or sampling changes reset accumulation.");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<OfflinePathTracingPass>(makePassEditor<OfflinePathTracingPass, drawSettings>("Offline rendering"));
}
