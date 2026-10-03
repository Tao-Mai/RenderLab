#pragma once

#include "asset/Asset.h"
#include "core/Reflect.h"

#include <bit>
#include <concepts>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

struct LightComponent;
struct RenderComponent;

struct ComponentDrawResult
{
    bool edited = false;
    bool resourcesChanged = false;

    ComponentDrawResult& operator|=(const ComponentDrawResult& other)
    {
        edited |= other.edited;
        resourcesChanged |= other.resourcesChanged;
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
ComponentDrawResult drawFields(LightComponent& value);
ComponentDrawResult drawFields(RenderComponent& value);

// Produce a complete optional-field patch, retaining texture/shader overrides.
MaterialOverride materialOverrideDiff(const MaterialAsset& stock, const MaterialAsset& preview);
bool hasMaterialOverrides(const MaterialOverride& patch);

template<class T>
ComponentDrawResult drawFields(T& value)
{
    ComponentDrawResult result;
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto member :
        std::define_static_array(ReflectedDataMembers(^^T, ctx)))
    {
        const std::string name(std::meta::identifier_of(member));
        ImGui::PushID(name.c_str());
        result.edited = drawField(name.c_str(), value.[:member:]) || result.edited;
        ImGui::PopID();
    }

    return result;
}

template<class T>
bool drawField(const char* label, T& value)
{
    if constexpr (FlagEnum<T>)
    {
        using Bits = std::make_unsigned_t<std::underlying_type_t<T>>;
        bool edited = false;
        if (!ImGui::TreeNode(label)) return false;

        template for (constexpr auto enumerator :
            std::define_static_array(std::meta::enumerators_of(^^T)))
        {
            constexpr Bits bit = static_cast<Bits>(std::to_underlying([:enumerator:]));
            if constexpr (std::has_single_bit(bit))
            {
                Bits bits = static_cast<Bits>(std::to_underlying(value));
                bool checked = (bits & bit) != 0;
                const std::string name(std::meta::identifier_of(enumerator));
                if (ImGui::Checkbox(name.c_str(), &checked))
                {
                    value = static_cast<T>(checked ? bits | bit : bits & ~bit);
                    edited = true;
                }
            }
        }

        ImGui::TreePop();
        return edited;
    }
    else if constexpr (std::is_enum_v<T>)
    {
        std::string preview = "<invalid>";
        template for (constexpr auto enumerator :
            std::define_static_array(std::meta::enumerators_of(^^T)))
        {
            if (value == [:enumerator:])
                preview = std::meta::identifier_of(enumerator);
        }

        bool edited = false;
        if (!ImGui::BeginCombo(label, preview.c_str())) return false;

        template for (constexpr auto enumerator :
            std::define_static_array(std::meta::enumerators_of(^^T)))
        {
            const std::string name(std::meta::identifier_of(enumerator));
            const bool selected = value == [:enumerator:];
            if (ImGui::Selectable(name.c_str(), selected))
            {
                value = [:enumerator:];
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
