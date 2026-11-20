#include "editor/SettingsWidgets.h"

#include <algorithm>
#include <cmath>
#include <numbers>

bool drawSettingsHeader(const char* label)
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
