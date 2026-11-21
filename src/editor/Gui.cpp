#include "editor/Gui.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <numbers>

namespace
{
bool drawSlider(const char* label, ImGuiDataType type, void* value, const void* minimum, const void* maximum,
                const char* format = nullptr, bool clampInput = true)
{
    const auto flags = clampInput ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
    return ImGui::SliderScalar(label, type, value, minimum, maximum, format, flags);
}

template <class Choice>
bool drawDropdown(const char* label, std::span<const Choice> choices, std::size_t& selected, const char* preview)
{
    DCHECK(preview || selected < choices.size());
    if (!ImGui::BeginCombo(label, preview ? preview : std::string_view{choices[selected]}.data()))
        return false;

    auto next = selected;
    for (std::size_t index = 0; index < choices.size(); ++index)
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
}

namespace Gui
{
bool Widgets::checkbox(const char* label, bool& value)
{
    return ImGui::Checkbox(label, &value);
}

bool Widgets::slider(const char* label, float& value, float minimum, float maximum,
                     const char* format, bool clampInput)
{
    return drawSlider(label, ImGuiDataType_Float, &value, &minimum, &maximum, format, clampInput);
}

bool Widgets::slider(const char* label, double& value, double minimum, double maximum)
{
    return drawSlider(label, ImGuiDataType_Double, &value, &minimum, &maximum);
}

bool Widgets::slider(const char* label, int32_t& value, int32_t minimum, int32_t maximum)
{
    return drawSlider(label, ImGuiDataType_S32, &value, &minimum, &maximum);
}

bool Widgets::slider(const char* label, uint32_t& value, uint32_t minimum, uint32_t maximum, bool clampInput)
{
    return drawSlider(label, ImGuiDataType_U32, &value, &minimum, &maximum, nullptr, clampInput);
}

bool Widgets::slider(const char* label, int64_t& value, int64_t minimum, int64_t maximum)
{
    return drawSlider(label, ImGuiDataType_S64, &value, &minimum, &maximum);
}

bool Widgets::input(const char* label, double& value)
{
    return ImGui::InputScalar(label, ImGuiDataType_Double, &value);
}

bool Widgets::input(const char* label, int64_t& value)
{
    return ImGui::InputScalar(label, ImGuiDataType_S64, &value);
}

bool Widgets::drag(const char* label, float& value, float speed, float minimum, float maximum, const char* format)
{
    return ImGui::DragFloat(label, &value, speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
}

bool Widgets::dragRange(const char* label, float& low, float& high, float speed,
                        float minimum, float maximum, const char* format)
{
    return ImGui::DragFloatRange2(label, &low, &high, speed, minimum, maximum, format, format,
                                  ImGuiSliderFlags_AlwaysClamp);
}

bool Widgets::dropdown(const char* label, std::span<const char* const> choices, std::size_t& selected,
                       const char* preview)
{
    return drawDropdown(label, choices, selected, preview);
}

bool Widgets::dropdown(const char* label, std::span<const std::string> choices, std::size_t& selected)
{
    return drawDropdown(label, choices, selected, nullptr);
}

Widgets::Group::Group(const char* label, bool defaultOpen) :
    open(ImGui::TreeNodeEx(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
{
}

Widgets::Group::~Group()
{
    if (open)
        ImGui::TreePop();
}

Widgets::Group Widgets::group(const char* label, bool defaultOpen)
{
    return Group(label, defaultOpen);
}

bool Widgets::resetHeader(const char* label)
{
    bool        reset      = false;
    const float buttonSize = ImGui::GetFrameHeight();
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable("Header", 2, ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, buttonSize);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SeparatorText(label);
        ImGui::TableNextColumn();
        reset = ImGui::Button("##ResetDefaults", ImVec2(buttonSize, buttonSize));
        ImGui::SetItemTooltip("Restore default parameters");

        const ImVec2    min = ImGui::GetItemRectMin();
        const ImVec2    center(min.x + buttonSize * 0.5f, min.y + buttonSize * 0.5f);
        const float     radius      = buttonSize * 0.23f;
        constexpr float kStartAngle = std::numbers::pi_v<float> * 0.15f;
        constexpr float kEndAngle   = std::numbers::pi_v<float> * 1.75f;
        const ImVec2    radial(std::cos(kEndAngle), std::sin(kEndAngle));
        const ImVec2    tangent(-radial.y, radial.x);
        const ImVec2    end(center.x + radial.x * radius, center.y + radial.y * radius);
        const float     arrowSize = radius * 0.65f;
        const ImU32     color     = ImGui::GetColorU32(ImGuiCol_Text);
        auto*           drawList  = ImGui::GetWindowDrawList();
        drawList->PathArcTo(center, radius, kStartAngle, kEndAngle);
        drawList->PathStroke(color, 0, std::max(1.0f, buttonSize * 0.06f));
        drawList->AddTriangleFilled(
            ImVec2(end.x + tangent.x * arrowSize, end.y + tangent.y * arrowSize),
            ImVec2(end.x + radial.x * arrowSize, end.y + radial.y * arrowSize),
            ImVec2(end.x - radial.x * arrowSize, end.y - radial.y * arrowSize),
            color
        );
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return reset;
}

bool Widgets::button(const char* label)
{
    return ImGui::Button(label);
}

bool Widgets::colorEdit(const char* label, glm::vec3& color)
{
    return ImGui::ColorEdit3(label, &color.x);
}

void Widgets::colorPreview(const char* label, const glm::vec3& color)
{
    ImGui::ColorButton(label, ImVec4{color.r, color.g, color.b, 1.0f});
}

void Widgets::text(std::string_view text)
{
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void Widgets::disabledText(const char* text)
{
    ImGui::TextDisabled("%s", text);
}

void Widgets::tooltip(const char* text)
{
    ImGui::SetItemTooltip("%s", text);
}

void Widgets::pushId(const char* id)
{
    ImGui::PushID(id);
}

void Widgets::popId()
{
    ImGui::PopID();
}
}
