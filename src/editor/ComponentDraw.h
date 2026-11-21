#pragma once

struct Component;
struct TransformComponent;

#include "asset/GltfMaterial.h"
#include "core/Metadata.h"
#include "core/TypeTraits.h"

#include <bit>
#include <concepts>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

struct PointLightComponent;
struct DirectionalLightComponent;
struct RectAreaLightComponent;
struct SpotLightComponent;
struct MeshAreaLightComponent;
struct EnvironmentLightComponent;
struct ProjectionLightComponent;
struct GoniometricLightComponent;
struct RenderComponent;

struct ComponentDrawResult
{
    bool edited = false;

    ComponentDrawResult& operator|=(const ComponentDrawResult& other)
    {
        edited |= other.edited;
        return *this;
    }
};

[[nodiscard]] ComponentDrawResult drawComponent(Component& component);

namespace component_draw
{
bool drawField(const char* label, bool& value);
bool drawField(const char* label, float& value);
bool drawField(const char* label, double& value);
bool drawField(const char* label, std::string& value);
bool drawField(const char* label, glm::vec2& value);
bool drawField(const char* label, glm::vec3& value);
bool drawField(const char* label, glm::vec4& value);
bool drawField(const char* label, glm::quat& value);

template<std::integral T> requires (!std::same_as<T, bool>)
bool drawField(const char* label, T& value)
{
    constexpr ImGuiDataType type = []
    {
        if constexpr (sizeof(T) == 1)
            return std::is_signed_v<T> ? ImGuiDataType_S8 : ImGuiDataType_U8;
        else if constexpr (sizeof(T) == 2)
            return std::is_signed_v<T> ? ImGuiDataType_S16 : ImGuiDataType_U16;
        else if constexpr (sizeof(T) == 4)
            return std::is_signed_v<T> ? ImGuiDataType_S32 : ImGuiDataType_U32;
        else
        {
            static_assert(sizeof(T) == 8);
            return std::is_signed_v<T> ? ImGuiDataType_S64 : ImGuiDataType_U64;
        }
    }();

    return ImGui::DragScalar(label, type, &value, 1.0f);
}

template<class T>
bool drawField(const char* label, AssetID<T>& value)
{
    return drawField(label, value.value);
}

template<class T>
bool drawField(const char* label, T& value);

template<class T>
ComponentDrawResult drawFields(T& value);

ComponentDrawResult drawFields(TransformComponent& value);
ComponentDrawResult drawFields(PointLightComponent& value);
ComponentDrawResult drawFields(DirectionalLightComponent& value);
ComponentDrawResult drawFields(RectAreaLightComponent& value);
ComponentDrawResult drawFields(SpotLightComponent& value);
ComponentDrawResult drawFields(MeshAreaLightComponent& value);
ComponentDrawResult drawFields(EnvironmentLightComponent& value);
ComponentDrawResult drawFields(ProjectionLightComponent& value);
ComponentDrawResult drawFields(GoniometricLightComponent& value);
ComponentDrawResult drawFields(RenderComponent& value);

// Produce a complete optional-field patch, retaining texture/shader overrides.
MaterialOverride materialOverrideDiff(const GltfMaterial& stock, const GltfMaterial& preview);
bool hasMaterialOverrides(const MaterialOverride& patch);

template<class T>
ComponentDrawResult drawFields(T& value)
{
    ComponentDrawResult result;
    metadata::visitFields(value, [&](const auto& field, auto& member)
    {
        ImGui::PushID(field.name);
        result.edited = drawField(field.name, member) || result.edited;
        ImGui::PopID();
    });

    return result;
}

template<class T>
bool drawField(const char* label, T& value)
{
    if constexpr (metadata::FlagEnum<T>)
    {
        using Bits = std::make_unsigned_t<std::underlying_type_t<T>>;
        bool edited = false;
        if (!ImGui::TreeNode(label)) return false;

        for (const auto& entry : metadata::Enum<T>::kValues)
        {
            const auto bit = static_cast<Bits>(std::to_underlying(entry.value));
            if (!std::has_single_bit(bit))
                continue;

            const auto bits = static_cast<Bits>(std::to_underlying(value));
            bool checked = (bits & bit) != 0;
            if (ImGui::Checkbox(entry.name, &checked))
            {
                value = static_cast<T>(checked ? bits | bit : bits & ~bit);
                edited = true;
            }
        }

        ImGui::TreePop();
        return edited;
    }
    else if constexpr (std::is_enum_v<T>)
    {
        const char* preview = "<invalid>";
        for (const auto& entry : metadata::Enum<T>::kValues)
            if (value == entry.value)
                preview = entry.name;

        bool edited = false;
        if (!ImGui::BeginCombo(label, preview)) return false;

        for (const auto& entry : metadata::Enum<T>::kValues)
        {
            const bool selected = value == entry.value;
            if (ImGui::Selectable(entry.name, selected))
            {
                value = entry.value;
                edited = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
        return edited;
    }
    else if constexpr (IsOptional<T>)
    {
        bool enabled = value.has_value();
        bool edited = ImGui::Checkbox(label, &enabled);
        if (edited)
        {
            if (enabled) value.emplace();
            else value.reset();
        }

        if (value)
        {
            ImGui::PushID(label);
            edited = drawField("Value", *value) || edited;
            ImGui::PopID();
        }
        return edited;
    }
    else if constexpr (IsVector<T> || IsMap<T>)
    {
        if (!ImGui::TreeNode(label)) return false;

        bool edited = false;
        if constexpr (IsVector<T>)
        {
            for (size_t index = 0; index < value.size(); ++index)
            {
                const std::string name = std::to_string(index);
                ImGui::PushID(name.c_str());
                edited = drawField(name.c_str(), value[index]) || edited;
                ImGui::PopID();
            }
        }
        else
        {
            for (auto& [key, item] : value)
            {
                const std::string name = [&]
                {
                    if constexpr (std::same_as<typename T::key_type, std::string>) return key;
                    else return std::to_string(key);
                }();
                ImGui::PushID(name.c_str());
                edited = drawField(name.c_str(), item) || edited;
                ImGui::PopID();
            }
        }

        ImGui::TreePop();
        return edited;
    }
    else if constexpr (std::is_class_v<T>)
    {
        if (!ImGui::TreeNode(label)) return false;
        const auto result = drawFields(value);
        ImGui::TreePop();
        return result.edited;
    }
    else
    {
        ImGui::TextDisabled("%s: not editable", label);
        return false;
    }
}
}
