#include "editor/PassEditorRegistry.h"

#include "render/Renderer.h"

#include <format>

void PassEditorRegistry::draw(Renderer& renderer, const PassEditorContext& context) const
{
    for (const auto& pass : renderer.renderPasses().activeInstances())
    {
        if (!pass.exposeEditor)
            continue;

        const auto editor = editors.find(pass.nativeType);
        if (editor == editors.end())
            continue;

        ImGui::PushID(pass.name.data());
        const auto label = std::format("{} ({})", editor->second.label, pass.name);
        if (ImGui::CollapsingHeader(label.c_str()))
        {
            const bool reset = ImGui::SmallButton("Reset settings");
            ImGui::SetItemTooltip("Restore the values from the graph.");
            if (reset)
                renderer.setPassOverrides(pass.type, pass.name, nlohmann::json::object());
            else if (auto edited = editor->second.draw(pass.object, context))
                renderer.setPassOverrides(pass.type, pass.name, passSettingsOverrides(*pass.scriptedSettings, *edited));
        }
        ImGui::PopID();
    }
}
