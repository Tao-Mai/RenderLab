#include "passes/postprocess/Bloom/Bloom.h"

#include "editor/PassEditor.h"
#include "editor/SettingsWidgets.h"

namespace
{
bool drawSettings(BloomSettings& bloom, const Bloom&, const PassEditorContext&)
{
    bool edited = false;
    edited |= ImGui::SliderFloat("Bloom intensity", &bloom.intensity, 0.0f, 1.0f, "%.3f");
    ImGui::SetItemTooltip("Fraction of scene light redistributed by bloom, not additive brightness.");
    edited |= ImGui::SliderFloat("Bloom scatter", &bloom.scatter, 0.05f, 0.95f, "%.2f");
    ImGui::SetItemTooltip("Higher values give coarser pyramid levels more weight and produce a wider glow.");
    edited |= ImGui::DragFloat(
        "Gaussian radius",
        &bloom.filterRadius,
        0.05f,
        0.1f,
        5.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );
    edited |= drawUnsignedSlider("Bloom levels", bloom.levels, 1, kMaxBloomLevels);
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<Bloom>(makePassEditor<Bloom, drawSettings>("Bloom"));
}
