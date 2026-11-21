#include "passes/postprocess/ExposurePass/ExposurePass.h"

#include "editor/EditorCameraConfig.h"
#include "editor/ExposureUi.h"
#include "editor/PassEditor.h"

namespace
{
bool drawSettings(Gui::Widgets& widgets, ExposurePassSettings& settings, const ExposurePass&,
                  const PassEditorContext& context)
{
    const bool edited = drawExposureSettings(widgets, settings);
    if (context.camera.exposure)
        widgets.disabledText("Exposure overridden by Editor camera");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<ExposurePass>("ExposurePass", makePassEditor<ExposurePass, drawSettings>("Exposure"));
}
