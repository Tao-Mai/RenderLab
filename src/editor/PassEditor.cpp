#include "editor/PassEditor.h"

#include "render/graph/RenderPass.h"

#include <format>
#include <imgui.h>

bool pass_editor::begin(const Falcor::RenderPass& pass, const PassEditorContext& context, std::string_view label)
{

    const auto& name  = pass.getName();
    const auto  title = std::format("{} ({})", label, name);
    if (!ImGui::CollapsingHeader(title.c_str()))
        return false;

    const bool reset = ImGui::SmallButton("Reset settings");
    ImGui::SetItemTooltip("Restore the values from the graph.");
    if (reset)
        context.submit(pass, std::nullopt);
    return !reset;
}
