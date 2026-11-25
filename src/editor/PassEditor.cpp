#include "editor/PassEditor.h"

#include "render/graph/RenderPass.h"

#include <imgui.h>

namespace
{
template <class T>
bool drawSlider(Gui::Widgets& widgets, const metadata::Property& field, const char* label, void* value)
{
    const auto& slider = field.passUi->slider;
    CHECK(slider, "numeric pass setting '{}' requires RL_PASS_SLIDER", field.name);
    const auto minimum = static_cast<T>(slider->minimum);
    const auto maximum = static_cast<T>(slider->maximum);
    if constexpr (std::is_same_v<T, float>)
        return widgets.slider(label, *static_cast<T*>(value), minimum, maximum, nullptr, true);
    else
        return widgets.slider(label, *static_cast<T*>(value), minimum, maximum);
}

bool drawField(Gui::Widgets& widgets, const metadata::Property& field, void* settings)
{
    auto* value = field.address(settings);
    const auto* label = field.passUi->label ? field.passUi->label : field.name;
    if (field.type == typeid(bool))
        return widgets.checkbox(label, *static_cast<bool*>(value));

    if (field.isEnum)
    {
        const auto& info = metadata::enumInfo(field.type);
        auto selected = info.index(value);
        if (!widgets.dropdown(label, info.names, selected))
            return false;

        info.select(value, selected);
        return true;
    }

    if (field.type == typeid(float))
        return drawSlider<float>(widgets, field, label, value);
    if (field.type == typeid(uint32_t))
        return drawSlider<uint32_t>(widgets, field, label, value);
    if (field.type == typeid(int32_t))
        return drawSlider<int32_t>(widgets, field, label, value);

    LOG_FATAL("pass setting '{}' requires a custom editor", field.name);
}
}

void pass_editor::draw(const RenderPass& pass, const PassEditorContext& context, Gui::Widgets& widgets)
{
    const auto& settings = pass.getPluginInfo().settings;
    if (!settings.get && !settings.drawEditor)
        return;

    const IDScope id(&pass);
    if (!ImGui::CollapsingHeader(pass.getName().c_str()))
        return;

    if (settings.get)
    {
        const bool reset = ImGui::SmallButton("Reset settings");
        ImGui::SetItemTooltip("Restore the values from the graph.");
        if (reset)
        {
            context.submit(pass, std::nullopt);
            return;
        }
    }

    if (settings.drawEditor)
        settings.drawEditor(pass, context, widgets);
}

bool pass_editor::drawFields(Gui::Widgets& widgets, void* settings, const metadata::Record& record)
{
    bool changed = false;
    for (const auto& field : record.fields)
    {
        if (!field.passUi)
            continue;

        const IDScope id(&field);
        changed |= drawField(widgets, field, settings);
        if (field.passUi->tooltip)
            widgets.tooltip(field.passUi->tooltip);
    }
    return changed;
}
