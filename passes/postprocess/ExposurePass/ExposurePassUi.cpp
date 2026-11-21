#include "passes/postprocess/ExposurePass/ExposurePass.h"

#include "editor/EditorCameraConfig.h"
#include "editor/ExposureUi.h"
#include "editor/PassEditor.h"

namespace
{
bool drawSettings(Falcor::Gui::Widgets& widgets, ExposurePassSettings& settings, const ExposurePass&,
                  const PassEditorContext& context)
{
    const bool edited = drawExposureSettings(widgets, settings);
    if (context.camera.exposure)
        widgets.disabledText("Exposure overridden by Editor camera");
    return edited;
}
}

void ExposurePass::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context->passEditor, "Exposure");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, ExposurePass>();
}
