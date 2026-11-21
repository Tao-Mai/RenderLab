#include "passes/postprocess/Bloom/Bloom.h"

#include "editor/PassEditor.h"

namespace
{
bool drawSettings(Gui::Widgets& widgets, BloomSettings& bloom, const Bloom&, const PassEditorContext&)
{
    bool edited = widgets.slider("Bloom intensity", bloom.intensity, 0.0f, 1.0f, "%.3f");
    widgets.tooltip("Fraction of scene light redistributed by bloom, not additive brightness.");
    edited |= widgets.slider("Bloom scatter", bloom.scatter, 0.05f, 0.95f, "%.2f");
    widgets.tooltip("Higher values give coarser pyramid levels more weight and produce a wider glow.");
    edited |= widgets.drag("Gaussian radius", bloom.filterRadius, 0.05f, 0.1f, 5.0f, "%.2f");
    edited |= widgets.slider("Bloom levels", bloom.levels, 1u, kMaxBloomLevels, false);
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<Bloom>("Bloom", makePassEditor<Bloom, drawSettings>("Bloom"));
}
