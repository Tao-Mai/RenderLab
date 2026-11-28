#include "render/Renderer.h"

#include "core/Context.h"
#include "editor/Gui.h"
#include "editor/PassEditor.h"

void Renderer::renderUI(const PassEditorContext& input)
{
    auto& ctx = context();
    auto& settings = ctx.config.renderer;
    Gui::Widgets widgets(ctx.config.editor.uiExpanded);
    widgets.dropdown("Render graph", settings.graph, {"Minimal", "ReSTIR"});

    if (widgets.resetHeader("Graph"))
        settings.parameters = {};

    widgets.checkbox("Enable accumulation", settings.parameters.accumulate);
    widgets.checkbox("Enable HDR", settings.parameters.hdr);
    widgets.dropdown("Post process", settings.parameters.postProcess, {"RenderLab", "Falcor"});
    widgets.checkbox("Enable bloom", settings.parameters.bloom);

    mpRenderGraph->renderUI(input, widgets, settings.passes);
}
