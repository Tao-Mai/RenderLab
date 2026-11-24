#include "editor/PassEditor.h"

#include "render/graph/RenderPass.h"

#include <format>
#include <imgui.h>

namespace
{
template <class T>
bool drawSlider(Gui::Widgets& widgets, const metadata::Property& field, void* value)
{
    CHECK(field.range, "numeric pass setting '{}' requires RL_SLIDER", field.name);
    const auto minimum = static_cast<T>(field.range->minimum);
    const auto maximum = static_cast<T>(field.range->maximum);
    if constexpr (std::is_same_v<T, float>)
        return widgets.slider(field.name, *static_cast<T*>(value), minimum, maximum, nullptr, true);
    else
        return widgets.slider(field.name, *static_cast<T*>(value), minimum, maximum);
}

bool drawField(Gui::Widgets& widgets, const metadata::Property& field, void* settings)
{
    auto* value = field.address(settings);
    if (field.type == typeid(bool))
        return widgets.checkbox(field.name, *static_cast<bool*>(value));

    if (field.isEnum)
    {
        const auto& info = metadata::enumInfo(field.type);
        auto selected = info.index(value);
        if (!widgets.dropdown(field.name, info.names, selected))
            return false;

        info.select(value, selected);
        return true;
    }

    if (field.type == typeid(float))
        return drawSlider<float>(widgets, field, value);
    if (field.type == typeid(uint32_t))
        return drawSlider<uint32_t>(widgets, field, value);
    if (field.type == typeid(int32_t))
        return drawSlider<int32_t>(widgets, field, value);

    LOG_FATAL("pass setting '{}' requires a custom editor", field.name);
}
}

bool pass_editor::begin(const RenderPass& pass, const PassEditorContext& context, std::string_view label)
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

bool pass_editor::drawFields(Gui::Widgets& widgets, void* settings, const metadata::Record& record)
{
    bool changed = false;
    for (const auto& field : record.fields)
        changed |= drawField(widgets, field, settings);
    return changed;
}
