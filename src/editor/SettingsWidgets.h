#pragma once

#include "render/RenderConfig.h"

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <imgui.h>

bool drawSettingsHeader(const char* label);

template <class Choices>
bool drawChoiceSetting(const char* label, const Choices& choices, size_t& selected)
{
    if (!ImGui::BeginCombo(label, std::string_view{choices[selected]}.data()))
        return false;

    auto next = selected;
    for (size_t index = 0; index < choices.size(); ++index)
    {
        if (ImGui::Selectable(std::string_view{choices[index]}.data(), index == selected))
            next = index;
        if (index == selected)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();

    const bool changed = next != selected;
    selected = next;
    return changed;
}

template <class T>
consteval ImGuiDataType numericSettingType()
{
    if constexpr (std::is_same_v<T, float>)
        return ImGuiDataType_Float;
    else if constexpr (std::is_same_v<T, double>)
        return ImGuiDataType_Double;
    else if constexpr (std::is_same_v<T, int32_t>)
        return ImGuiDataType_S32;
    else if constexpr (std::is_same_v<T, uint32_t>)
        return ImGuiDataType_U32;
    else
    {
        static_assert(std::is_same_v<T, int64_t>, "unsupported numeric setting");
        return ImGuiDataType_S64;
    }
}

template <class T>
bool drawNumericInput(const char* label, T& value)
{
    return ImGui::InputScalar(label, numericSettingType<T>(), &value);
}

template <class T>
bool drawNumericSlider(const char* label, T& value, T minimum, T maximum)
{
    return ImGui::SliderScalar(
        label, numericSettingType<T>(), &value, &minimum, &maximum, nullptr, ImGuiSliderFlags_AlwaysClamp
    );
}

template <class Enum>
bool drawEnumCombo(const char* label, Enum& value, const char* items)
{
    int index = static_cast<int>(value);
    if (!ImGui::Combo(label, &index, items))
        return false;

    value = static_cast<Enum>(index);
    return true;
}

inline bool drawUnsignedSlider(
    const char*      label,
    uint32_t&        value,
    uint32_t         minimum,
    uint32_t         maximum,
    ImGuiSliderFlags flags = ImGuiSliderFlags_None
)
{
    int selected = static_cast<int>(value);
    if (!ImGui::SliderInt(label, &selected, static_cast<int>(minimum), static_cast<int>(maximum), "%d", flags))
        return false;

    value = static_cast<uint32_t>(selected);
    return true;
}

inline bool drawMaxBounces(uint32_t& maxBounces)
{
    const bool edited =
        drawUnsignedSlider("Max bounces", maxBounces, kMinPathBounces, kMaxPathBounces, ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
