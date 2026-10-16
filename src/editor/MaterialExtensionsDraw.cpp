#include "editor/MaterialExtensionsDraw.h"

#include "asset/PbrMaterialAsset.h"

#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <limits>
#include <numbers>

namespace
{
constexpr float kMaxValue                   = std::numeric_limits<float>::max();
constexpr float kDefaultIor                 = 1.5f;
constexpr float kMinimumAttenuationDistance = 0.001f;

bool dragFloat(
    const char* label,
    float&      value,
    float       minimum = 0.0f,
    float       maximum = kMaxValue,
    float       speed   = 0.01f,
    const char* format  = "%.3f"
)
{
    return ImGui::DragFloat(label, &value, speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
}

bool drawFactor(const char* label, float& value) { return dragFloat(label, value, 0.0f, 1.0f); }

void drawTexture(const char* label, const std::optional<TextureBinding>& texture)
{
    ImGui::Text("%s: %s", label, texture ? texture->textureID.c_str() : "None");
}

template <class T, class F>
bool drawExtension(const char* label, std::optional<T>& extension, F&& draw, bool rendered = false)
{
    if (!extension)
        return false;

    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    if (!rendered)
        ImGui::TextDisabled("Data only - shading not implemented");
    const bool edited = draw(*extension);
    ImGui::PopID();
    return edited;
}
}  // namespace

bool drawMaterialExtensions(PbrMaterialAsset& material)
{
    bool edited = false;
    edited |= drawExtension(
        "Clearcoat",
        material.clearcoat,
        [](auto& layer)
        {
            bool changed = drawFactor("Factor", layer.factor);
            changed |= drawFactor("Roughness", layer.roughnessFactor);
            ImGui::BeginDisabled(!layer.normalTexture);
            changed |= dragFloat("Normal Scale", layer.normalScale, 0.0f, kMaxValue, 0.05f);
            ImGui::EndDisabled();
            drawTexture("Intensity texture", layer.texture);
            drawTexture("Roughness texture", layer.roughnessTexture);
            drawTexture("Normal texture", layer.normalTexture);
            return changed;
        },
        true
    );

    edited |= drawExtension(
        "Sheen",
        material.sheen,
        [](auto& layer)
        {
            bool changed = ImGui::ColorEdit3("Color", glm::value_ptr(layer.colorFactor), ImGuiColorEditFlags_Float);
            changed |= drawFactor("Roughness", layer.roughnessFactor);
            drawTexture("Color texture", layer.colorTexture);
            drawTexture("Roughness texture", layer.roughnessTexture);
            return changed;
        },
        true
    );

    edited |= drawExtension(
        "Anisotropy",
        material.anisotropy,
        [](auto& layer)
        {
            bool  changed  = drawFactor("Strength", layer.strength);
            float rotation = layer.rotation * (180.0f / std::numbers::pi_v<float>);
            if (ImGui::DragFloat("Rotation", &rotation, 0.5f, 0.0f, 0.0f, "%.1f deg"))
            {
                layer.rotation = rotation * (std::numbers::pi_v<float> / 180.0f);
                changed        = true;
            }
            drawTexture("Direction / strength texture", layer.texture);
            return changed;
        }
    );

    edited |= drawExtension(
        "Transmission",
        material.transmission,
        [](auto& layer)
        {
            const bool changed = drawFactor("Factor", layer.factor);
            drawTexture("Factor texture", layer.texture);
            return changed;
        }
    );

    edited |= drawExtension(
        "Volume",
        material.volume,
        [](auto& layer)
        {
            bool changed = dragFloat("Thickness (m)", layer.thicknessFactor);
            changed |= ImGui::ColorEdit3(
                "Attenuation Color",
                glm::value_ptr(layer.attenuationColor),
                ImGuiColorEditFlags_Float
            );
            bool finiteDistance = layer.attenuationDistance.has_value();
            if (ImGui::Checkbox("Finite Attenuation Distance", &finiteDistance))
            {
                if (finiteDistance)
                    layer.attenuationDistance = 1.0f;
                else
                    layer.attenuationDistance.reset();
                changed = true;
            }
            if (layer.attenuationDistance)
                changed |=
                    dragFloat("Attenuation Distance (m)", *layer.attenuationDistance, kMinimumAttenuationDistance);
            drawTexture("Thickness texture", layer.thicknessTexture);
            return changed;
        }
    );

    edited |= drawExtension(
        "IOR",
        material.ior,
        [](float& ior)
        {
            bool infinite = ior == 0.0f;
            bool changed  = ImGui::Checkbox("Infinite IOR", &infinite);
            if (changed)
                ior = infinite ? 0.0f : kDefaultIor;
            if (!infinite)
                changed |= dragFloat("IOR", ior, 1.0f);
            return changed;
        }
    );

    edited |= drawExtension(
        "Specular",
        material.specular,
        [](auto& layer)
        {
            bool changed = drawFactor("Factor", layer.factor);
            changed |= ImGui::ColorEdit3(
                "Color",
                glm::value_ptr(layer.colorFactor),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR
            );
            drawTexture("Factor texture", layer.texture);
            drawTexture("Color texture", layer.colorTexture);
            return changed;
        }
    );

    edited |= drawExtension(
        "Iridescence",
        material.iridescence,
        [](auto& layer)
        {
            bool changed = drawFactor("Factor", layer.factor);
            changed |= dragFloat("Film IOR", layer.ior, 1.0f);
            changed |= dragFloat("Minimum Thickness (nm)", layer.thicknessMinimum, 0.0f, layer.thicknessMaximum, 1.0f);
            changed |=
                dragFloat("Maximum Thickness (nm)", layer.thicknessMaximum, layer.thicknessMinimum, kMaxValue, 1.0f);
            drawTexture("Factor texture", layer.texture);
            drawTexture("Thickness texture", layer.thicknessTexture);
            return changed;
        }
    );

    edited |=
        drawExtension("Dispersion", material.dispersion, [](float& value) { return dragFloat("Dispersion", value); });
    edited |= drawExtension(
        "Emissive Strength",
        material.emissiveStrength,
        [](float& value) { return dragFloat("Strength", value, 0.0f, kMaxValue, 0.1f); }
    );
    edited |= drawExtension("Unlit", material.unlit, [](bool& value) { return ImGui::Checkbox("Unlit", &value); });

    edited |= drawExtension(
        "Diffuse Transmission",
        material.diffuseTransmission,
        [](auto& layer)
        {
            bool changed = drawFactor("Factor", layer.factor);
            changed |= ImGui::ColorEdit3("Color", glm::value_ptr(layer.colorFactor), ImGuiColorEditFlags_Float);
            drawTexture("Factor texture", layer.texture);
            drawTexture("Color texture", layer.colorTexture);
            return changed;
        }
    );
    return edited;
}
