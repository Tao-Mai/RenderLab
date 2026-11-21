#include "editor/GraphSettingsUi.h"

#include "core/ConfigManager.h"
#include "core/Logger.h"
#include "editor/Gui.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <optional>
#include <utility>

namespace
{
template <class T>
bool drawGraphNumber(Gui::Widgets& widgets, GraphParameterControl& parameter)
{
    auto value = parameter.value.get<T>();
    const char* label = parameter.label.c_str();
    const bool changed = parameter.bounds.is_null() ? widgets.input(label, value) :
        widgets.slider(label, value, parameter.bounds[0].get<T>(), parameter.bounds[1].get<T>());
    if constexpr (std::is_floating_point_v<T>)
    {
        if (changed && std::isfinite(value))
            parameter.value = value;
    }
    else if (changed)
        parameter.value = value;

    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawGraphParameter(Gui::Widgets& widgets, GraphParameterControl& parameter)
{
    const char* label = parameter.label.c_str();
    if (parameter.value.is_boolean())
    {
        auto value = parameter.value.get<bool>();
        if (!widgets.checkbox(label, value))
            return false;

        parameter.value = value;
        return true;
    }
    if (parameter.value.is_string())
    {
        const auto& value = parameter.value.get_ref<const std::string&>();
        const auto& choices = parameter.choices;
        size_t selected = std::ranges::find(choices, value) - choices.begin();
        if (!widgets.dropdown(label, choices, selected))
            return false;

        parameter.value = choices[selected];
        return true;
    }
    if (parameter.value.is_number_integer())
        return drawGraphNumber<int64_t>(widgets, parameter);

    DCHECK(parameter.value.is_number_float());
    return drawGraphNumber<double>(widgets, parameter);
}
}

void drawGraphSettings(ConfigManager& config, std::span<GraphParameterControl> controls)
{
    if (controls.empty())
        return;

    Gui::Widgets widgets;
    const bool reset = widgets.resetHeader("Graph");
    std::optional<nlohmann::json> overrides;
    for (auto& parameter : controls)
    {
        if (reset)
            parameter.value = parameter.defaultValue;

        widgets.pushId(parameter.name.c_str());
        const bool edited = drawGraphParameter(widgets, parameter);
        widgets.popId();
        if (!edited && !reset)
            continue;

        if (!overrides)
            overrides = config.graphParameterOverrides();
        if (parameter.value == parameter.defaultValue)
            overrides->erase(parameter.name);
        else
            (*overrides)[parameter.name] = parameter.value;
    }

    if (overrides)
        config.setGraphParameterOverrides(std::move(*overrides));
}
