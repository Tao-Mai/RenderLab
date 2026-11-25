#include "editor/PassEditor.h"

#include "render/graph/RenderPass.h"

#include <imgui.h>

void pass_editor::draw(
    const RenderPass& pass, const PassEditorContext& context, Gui::Widgets& widgets, RenderPassOptions& options
)
{
    const auto draw = pass.getSettingsBinding().drawEditor;
    if (!draw)
        return;

    const IDScope id(&pass);
    if (!ImGui::CollapsingHeader(pass.getName().c_str()))
        return;

    const bool reset = ImGui::SmallButton("Reset settings");
    ImGui::SetItemTooltip("Restore the default settings.");
    draw(pass, context, widgets, options, reset);
}
