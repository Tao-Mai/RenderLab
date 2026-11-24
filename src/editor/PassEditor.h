#pragma once
#include <slang-rhi.h>

#include "core/Serializer.h"
#include "editor/Gui.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

class RenderPass;
struct EditorCameraConfig;

struct PassEditorContext
{
    rhi::IDevice&                                                                 device;
    const EditorCameraConfig&                                                     camera;
    std::function<void(const RenderPass&, std::optional<nlohmann::json>)> submit;
};

namespace pass_editor
{
bool begin(const RenderPass& pass, const PassEditorContext& context, std::string_view label);

template <class Field, class T>
static bool drawField(Gui::Widgets& widgets, const Field& field, T& value)
{
    if constexpr (std::is_same_v<T, bool>)
        return widgets.checkbox(field.name, value);
    else if constexpr (std::is_enum_v<T>)
    {
        constexpr auto kOptions = []
        {
            constexpr const auto&                    kEntries = metadata::Enum<T>::kValues;
            std::array<T, kEntries.size()>           values{};
            std::array<const char*, kEntries.size()> labels{};
            for (size_t index = 0; index < kEntries.size(); ++index)
            {
                values[index] = kEntries[index].value;
                labels[index] = kEntries[index].name;
            }
            return std::pair{values, labels};
        }();
        const auto& [values, labels] = kOptions;
        size_t selected              = std::ranges::find(values, value) - values.begin();
        if (!widgets.dropdown(field.name, labels, selected))
            return false;

        value = values[selected];
        return true;
    }
    else
    {
        static_assert(
            std::is_same_v<T, float> || std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>,
            "provide a custom editor for this settings type"
        );
        static_assert(requires { field.range; }, "numeric settings require RL_SLIDER");
        const auto minimum = static_cast<T>(field.range.minimum);
        const auto maximum = static_cast<T>(field.range.maximum);
        if constexpr (std::is_same_v<T, float>)
            return widgets.slider(field.name, value, minimum, maximum, nullptr, true);
        else
            return widgets.slider(field.name, value, minimum, maximum);
    }
}
template <class T>
static bool drawFields(Gui::Widgets& widgets, T& settings)
{
    bool changed = false;
    metadata::visitFields(
        settings,
        [&](const auto& field, auto& value) { changed |= drawField(widgets, field, value); }
    );
    return changed;
}
}  // namespace pass_editor

template <auto Draw = nullptr, class Pass, class Settings>
void drawPassEditor(
    Gui::Widgets&    widgets,
    const Pass&              pass,
    const Settings&          parameters,
    const PassEditorContext& context,
    std::string_view         label
)
{
    if (!pass_editor::begin(pass, context, label))
        return;

    auto settings = parameters;
    bool changed;
    if constexpr (Draw != nullptr)
        changed = Draw(widgets, settings, pass, context);
    else
        changed = pass_editor::drawFields(widgets, settings);

    if (changed)
        context.submit(pass, Serialize(settings));
}
