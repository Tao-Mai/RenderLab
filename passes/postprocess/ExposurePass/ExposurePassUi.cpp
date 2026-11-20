#include "passes/postprocess/ExposurePass/ExposurePass.h"

#include "editor/PassEditorRegistry.h"

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

void registerExposurePassEditor(PassEditorRegistry& editors)
{
    editors.add<ExposurePass, drawSettings>("Exposure");
}
