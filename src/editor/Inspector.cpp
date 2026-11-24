#include "editor/Inspector.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include "asset/AssetManager.h"
#include "asset/SceneAsset.h"
#include "editor/ComponentDraw.h"
#include "editor/EditorCameraConfig.h"
#include "editor/Gui.h"
#include "scene/AActor.h"

bool drawCameraInspector(EditorCameraConfig& camera)
{
    bool edited = false;
    if (ImGui::CollapsingHeader("Editor camera"))
    {
        edited |= ImGui::DragFloat3("Camera position", glm::value_ptr(camera.position), 0.05f);
        edited |= ImGui::DragFloat("Camera yaw", &camera.yaw, 0.2f, 0.0f, 0.0f, "%.1f deg");
        edited |= ImGui::DragFloat(
            "Camera pitch",
            &camera.pitch,
            0.2f,
            -89.0f,
            89.0f,
            "%.1f deg",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |= ImGui::DragFloat(
            "Camera FOV",
            &camera.fieldOfView,
            0.2f,
            20.0f,
            90.0f,
            "%.1f deg",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |= ImGui::DragFloat(
            "Lens radius",
            &camera.lensRadius,
            0.001f,
            0.0f,
            1000.0f,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |= ImGui::DragFloat(
            "Focal distance",
            &camera.focalDistance,
            0.1f,
            0.001f,
            1e6f,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp
        );
        ImGui::SetItemTooltip("Depth-of-field settings are saved; rendering support is pending.");
        edited |= ImGui::DragFloat(
            "Camera near",
            &camera.nearPlane,
            0.01f,
            0.001f,
            camera.farPlane - 0.001f,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |= ImGui::DragFloat(
            "Camera far",
            &camera.farPlane,
            0.5f,
            camera.nearPlane + 0.001f,
            100000.0f,
            "%.1f",
            ImGuiSliderFlags_AlwaysClamp
        );

        ImGui::SeparatorText("Navigation");
        edited |= ImGui::DragFloat(
            "Camera sensitivity",
            &camera.mouseSensitivity,
            0.001f,
            0.001f,
            10.0f,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |=
            ImGui::DragFloat("Camera speed", &camera.speed, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        edited |= ImGui::DragFloat(
            "Camera sprint multiplier",
            &camera.sprintMultiplier,
            0.1f,
            1.0f,
            100.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        );
    }


    return edited;
}

bool drawEnvironmentInspector(SceneAsset& scene, const AssetManager& assets)
{
    Gui::Widgets widgets;
    auto& current = scene.environmentMap;
    bool edited = false;
    if (widgets.resetHeader("Environment") && current)
    {
        current.reset();
        edited = true;
    }

    const char* preview = current ? current->c_str() : "Select environment map...";
    if (!ImGui::BeginCombo("Environment map", preview))
        return edited;

    auto ids = assets.loadedIds<EnvironmentMapAsset>();
    std::ranges::sort(ids, {}, &EnvironmentMapAsset::ID::value);
    for (const auto& id : ids)
    {
        const bool selected = current && *current == id;
        if (ImGui::Selectable(id.c_str(), selected) && !selected)
        {
            current = id;
            edited = true;
        }
    }
    ImGui::EndCombo();
    return edited;
}

bool drawObjectInspector(AActor* object)
{
    bool edited = false;
    ImGui::SeparatorText("Object");
    if (object != nullptr)
    {
        ImGui::PushID(object);
        edited = component_draw::drawField("Name", object->name) || edited;
        ComponentDrawResult changes;
        for (const auto& component : object->components)
        {
            if (!component)
                continue;
            changes |= drawComponent(*component);
        }
        edited |= changes.edited;
        ImGui::PopID();
    }
    else
    {
        ImGui::TextUnformatted("No selection");
    }


    return edited;
}
