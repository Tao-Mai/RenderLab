#include "editor/Inspector.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include "editor/ComponentDraw.h"
#include "editor/EditorCameraConfig.h"
#include "scene/AActor.h"

bool drawExposureSettings(ExposureConfig& exposure)
{
    int  mode   = static_cast<int>(exposure.mode);
    bool edited = ImGui::Combo("Exposure mode", &mode, "Manual\0Automatic\0");
    if (edited)
    {
        exposure.mode = static_cast<ExposureMode>(mode);
    }

    if (exposure.mode == ExposureMode::Manual)
    {
        edited =
            ImGui::DragFloat("EV100", &exposure.ev100, 0.05f, -10.0f, 30.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp) ||
            edited;
        ImGui::SetItemTooltip("Higher EV100 darkens the image; +1 halves linear brightness.");
    }
    else
    {
        auto& automatic = exposure.automatic;
        edited          = ImGui::DragFloatRange2(
                     "EV100 range",
                     &automatic.minEv100,
                     &automatic.maxEv100,
                     0.05f,
                     -16.0f,
                     24.0f,
                     "%.2f",
                     "%.2f",
                     ImGuiSliderFlags_AlwaysClamp
                 ) ||
            edited;

        edited = ImGui::DragFloat(
                     "Adapt to bright",
                     &automatic.brightAdaptationSpeed,
                     0.05f,
                     0.01f,
                     20.0f,
                     "%.2f /s",
                     ImGuiSliderFlags_AlwaysClamp
                 ) ||
            edited;
        edited = ImGui::DragFloat(
                     "Adapt to dark",
                     &automatic.darkAdaptationSpeed,
                     0.05f,
                     0.01f,
                     20.0f,
                     "%.2f /s",
                     ImGuiSliderFlags_AlwaysClamp
                 ) ||
            edited;

        edited = ImGui::SliderFloat(
                     "Low percentile",
                     &automatic.lowPercentile,
                     0.0f,
                     automatic.highPercentile - 0.001f,
                     "%.3f"
                 ) ||
            edited;
        edited = ImGui::SliderFloat(
                     "High percentile",
                     &automatic.highPercentile,
                     automatic.lowPercentile + 0.001f,
                     1.0f,
                     "%.3f"
                 ) ||
            edited;
    }

    edited = ImGui::DragFloat(
                 "Exposure compensation (stops)",
                 &exposure.exposureCompensation,
                 0.05f,
                 -10.0f,
                 10.0f,
                 "%.2f",
                 ImGuiSliderFlags_AlwaysClamp
             ) ||
        edited;

    return edited;
}

bool drawCameraInspector(EditorCameraConfig& camera, const ExposureConfig& globalExposure)
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

        ImGui::SeparatorText("Exposure");
        ImGui::PushID("CameraExposure");
        bool overrideExposure = camera.exposure.has_value();
        if (ImGui::Checkbox("Override global exposure", &overrideExposure))
        {
            if (overrideExposure)
            {
                camera.exposure = globalExposure;
            }
            else
            {
                camera.exposure.reset();
            }
            edited = true;
        }

        if (camera.exposure)
        {
            edited = drawExposureSettings(*camera.exposure) || edited;
        }
        ImGui::PopID();

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
