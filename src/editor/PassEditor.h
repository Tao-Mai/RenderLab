#pragma once

#include "editor/Gui.h"
#include "render/graph/PassSettings.h"
#include "render/plugins/PluginManager.h"

#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

class Renderer;
class VulkanContext;
struct EditorCameraConfig;

struct PassEditorContext
{
    const VulkanContext&      vulkan;
    const EditorCameraConfig& camera;
};

namespace pass_editor
{
template <class Field, class T>
static bool drawField(Gui::Widgets& widgets, const Field& field, T& value)
{
    if constexpr (std::is_same_v<T, bool>)
        return widgets.checkbox(field.name, value);
    else if constexpr (std::is_enum_v<T>)
    {
        constexpr auto kOptions = []
        {
            constexpr const auto& kEntries = metadata::Enum<T>::kValues;
            std::array<T, kEntries.size()> values{};
            std::array<const char*, kEntries.size()> labels{};
            for (size_t index = 0; index < kEntries.size(); ++index)
            {
                values[index] = kEntries[index].value;
                labels[index] = kEntries[index].name;
            }
            return std::pair{values, labels};
        }();
        const auto& [values, labels] = kOptions;
        size_t selected = std::ranges::find(values, value) - values.begin();
        if (!widgets.dropdown(field.name, labels, selected))
            return false;

        value = values[selected];
        return true;
    }
    else
    {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>,
                      "provide a custom editor for this settings type");
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
    metadata::visitFields(settings, [&](const auto& field, auto& value)
    {
        changed |= drawField(widgets, field, value);
    });
    return changed;
}
}

template <class Pass, auto Draw = nullptr>
PassEditor makePassEditor(std::string label)
{
    return {
        std::move(label),
        [](Gui::Widgets& widgets, const RenderPass& object, const PassEditorContext& context) -> std::optional<nlohmann::json>
        {
            const auto& pass = static_cast<const Pass&>(object);
            auto settings = pass.settings();
            bool changed;
            if constexpr (Draw != nullptr)
                changed = Draw(widgets, settings, pass, context);
            else
                changed = pass_editor::drawFields(widgets, settings);

            if (changed)
                return Serialize(settings);
            return std::nullopt;
        },
    };
}

void drawPassEditors(Renderer& renderer, const PassEditorContext& context);
