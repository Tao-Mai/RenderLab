#include "editor/PassEditor.h"

#include "render/Renderer.h"

#include <format>

void drawPassEditors(Renderer& renderer, const PassEditorContext& context)
{
    for (const auto* instance : renderer.graphPasses().activePasses())
    {
        const auto& pass = *instance;
        if (!pass.exposeEditor)
            continue;

        const auto& editor = renderer.plugins().editor(pass.type);
        if (!editor.draw)
            continue;

        ImGui::PushID(pass.name.data());
        const auto label = std::format("{} ({})", editor.label, pass.name);
        if (ImGui::CollapsingHeader(label.c_str()))
        {
            const bool reset = ImGui::SmallButton("Reset settings");
            ImGui::SetItemTooltip("Restore the values from the graph.");
            if (reset)
                renderer.setPassOverrides(pass.type, pass.name, nlohmann::json::object());
            else if (auto edited = editor.draw(*pass.pass, context))
                renderer.setPassOverrides(pass.type, pass.name, passSettingsOverrides(pass.scriptedProperties, *edited));
        }
        ImGui::PopID();
    }
}
