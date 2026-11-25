#include "shaders/passes/postprocess/ExposurePass/ExposurePass.h"

bool ExposurePass::drawSettings(
    Gui::Widgets& widgets, ExposurePassSettings& exposure, const ExposurePass&, const PassEditorContext&
)
{
    bool edited = widgets.dropdown("Exposure mode", exposure.mode, {"Manual", "Automatic"});
    if (exposure.mode == ExposureMode::Manual)
    {
        edited |= widgets.drag("EV100", exposure.ev100, 0.05f, -10.0f, 30.0f, "%.2f");
        widgets.tooltip("Higher EV100 darkens the image; +1 halves linear brightness.");
    }
    else
    {
        auto& automatic = exposure.automatic;
        edited |= widgets.dragRange("EV100 range", automatic.minEv100, automatic.maxEv100, 0.05f, -16.0f, 24.0f, "%.2f");
        edited |= widgets.drag("Adapt to bright", automatic.brightAdaptationSpeed, 0.05f, 0.01f, 20.0f, "%.2f /s");
        edited |= widgets.drag("Adapt to dark", automatic.darkAdaptationSpeed, 0.05f, 0.01f, 20.0f, "%.2f /s");
        edited |= widgets.slider("Low percentile", automatic.lowPercentile, 0.0f, automatic.highPercentile - 0.001f);
        edited |= widgets.slider("High percentile", automatic.highPercentile, automatic.lowPercentile + 0.001f, 1.0f);
    }

    edited |= widgets.drag("Exposure compensation (stops)", exposure.exposureCompensation, 0.05f, -10.0f, 10.0f, "%.2f");
    return edited;
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ExposurePass>();
}
