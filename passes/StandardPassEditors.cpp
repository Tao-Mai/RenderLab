#include "passes/StandardPassEditors.h"

#include "editor/PassEditorRegistry.h"
#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "passes/SVGF/SVGF.h"

void registerShadowPassEditor(PassEditorRegistry& editors);
void registerRayTracingPassEditor(PassEditorRegistry& editors);
void registerOfflinePathTracingPassEditor(PassEditorRegistry& editors);
void registerExposurePassEditor(PassEditorRegistry& editors);
void registerBloomEditor(PassEditorRegistry& editors);
void registerFalcorPostProcessEditor(PassEditorRegistry& editors);

PassEditorRegistry standardPassEditors()
{
    PassEditorRegistry editors;
    registerShadowPassEditor(editors);
    registerRayTracingPassEditor(editors);
    registerOfflinePathTracingPassEditor(editors);
    editors.add<SVGF>("SVGF");
    registerExposurePassEditor(editors);
    registerBloomEditor(editors);
    editors.add<ToneMappingPass>("Tone mapping");
    registerFalcorPostProcessEditor(editors);
    return editors;
}
