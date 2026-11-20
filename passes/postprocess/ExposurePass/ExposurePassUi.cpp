#include "passes/postprocess/ExposurePass/ExposurePass.h"

#include "editor/PassEditor.h"

#include "editor/EditorCameraConfig.h"
#include "editor/Inspector.h"

namespace
{
bool drawSettings(ExposurePassSettings& settings, const ExposurePass&, const PassEditorContext& context)
{
    const bool edited = drawExposureSettings(settings);
    if (context.camera.exposure)
        ImGui::TextDisabled("Exposure overridden by Editor camera");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<ExposurePass>(makePassEditor<ExposurePass, drawSettings>("Exposure"));
}
