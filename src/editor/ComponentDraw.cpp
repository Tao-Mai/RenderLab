#include "editor/ComponentDraw.h"

#include "asset/ApplyOptionalFields.h"
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "scene/SceneTypes.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_stdlib.h>

namespace component_draw
{
bool drawField(const char* label, bool& value) { return ImGui::Checkbox(label, &value); }
bool drawField(const char* label, float& value) { return ImGui::DragFloat(label, &value, 0.05f); }
bool drawField(const char* label, double& value)
{
    return ImGui::DragScalar(label, ImGuiDataType_Double, &value, 0.05f);
}

bool drawField(const char* label, std::string& value) { return ImGui::InputText(label, &value); }
bool drawField(const char* label, glm::vec2& value)
{
    return ImGui::DragFloat2(label, glm::value_ptr(value), 0.05f);
}
bool drawField(const char* label, glm::vec3& value)
{
    return ImGui::DragFloat3(label, glm::value_ptr(value), 0.05f);
}
bool drawField(const char* label, glm::vec4& value)
{
    return ImGui::DragFloat4(label, glm::value_ptr(value), 0.05f);
}
bool drawField(const char* label, glm::quat& value)
{
    glm::vec3 degrees = glm::degrees(glm::eulerAngles(glm::normalize(value)));
    if (!ImGui::DragFloat3(label, glm::value_ptr(degrees), 0.25f, 0.0f, 0.0f, "%.1f deg"))
        return false;

    glm::quat rotation = glm::normalize(glm::quat(glm::radians(degrees)));
    if (glm::dot(rotation, value) < 0.0f) rotation = -rotation;
    value = rotation;
    return true;
}

ComponentDrawResult drawFields(TransformComponent& transform)
{
    ComponentDrawResult result;
    result.edited = ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.05f);
    glm::vec3 degrees = transform.rotationEulerDegrees();
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(degrees), 0.25f, 0.0f, 0.0f, "%.1f deg"))
    {
        transform.setRotationEulerDegrees(degrees);
        result.edited = true;
    }

    if (ImGui::DragFloat3("Scale", glm::value_ptr(transform.scale), 0.01f))
    {
        transform.scale = glm::max(transform.scale, glm::vec3{0.001f});
        result.edited = true;
    }
    return result;
}

ComponentDrawResult drawFields(LightComponent& light)
{
    ComponentDrawResult result;
    result.edited = drawField("Type", light.type);
    result.edited = ImGui::ColorEdit3("Color", glm::value_ptr(light.color)) || result.edited;
    result.edited = ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100000.0f,
                                   "%.3f", ImGuiSliderFlags_AlwaysClamp) || result.edited;

    if (light.type == LightComponent::Type::Point || light.type == LightComponent::Type::Spot)
        result.edited = ImGui::DragFloat("Range", &light.range, 0.1f, 0.01f, 100000.0f,
                                       "%.3f", ImGuiSliderFlags_AlwaysClamp) || result.edited;

    if (light.type == LightComponent::Type::Spot)
    {
        float inner = glm::degrees(std::acos(std::clamp(light.cosInner, -1.0f, 1.0f)));
        float outer = glm::degrees(std::acos(std::clamp(light.cosOuter, -1.0f, 1.0f)));
        if (ImGui::DragFloat("Inner Angle", &inner, 0.25f, 0.0f, outer,
                             "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
        {
            light.cosInner = std::cos(glm::radians(inner));
            result.edited = true;
        }
        if (ImGui::DragFloat("Outer Angle", &outer, 0.25f, inner, 89.0f,
                             "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
        {
            light.cosOuter = std::cos(glm::radians(outer));
            result.edited = true;
        }
    }
    if (light.type == LightComponent::Type::RectArea)
        result.edited = ImGui::DragFloat2("Area Size", glm::value_ptr(light.areaSize), 0.05f,
                                        0.01f, 100000.0f, "%.3f",
                                        ImGuiSliderFlags_AlwaysClamp) || result.edited;

    result.edited = drawField("Enabled", light.enabled) || result.edited;
    result.edited = drawField("Cast Shadow", light.castShadow) || result.edited;
    return result;
}

MaterialOverride materialOverrideDiff(const MaterialAsset& stock, const MaterialAsset& preview)
{
    MaterialOverride patch;
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto member :
        std::define_static_array(ReflectedDataMembers(^^MaterialOverride, ctx)))
    {
        using Field = std::remove_cvref_t<decltype(patch.[:member:])>;
        if constexpr (IsOptional<Field>)
            if (preview.[:member:] != stock.[:member:]) patch.[:member:] = preview.[:member:];
    }
    return patch;
}

bool hasMaterialOverrides(const MaterialOverride& patch)
{
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto member :
        std::define_static_array(ReflectedDataMembers(^^MaterialOverride, ctx)))
    {
        using Field = std::remove_cvref_t<decltype(patch.[:member:])>;
        if constexpr (IsOptional<Field>)
            if (patch.[:member:]) return true;
    }
    return false;
}

namespace
{
bool drawMeshSelection(RenderComponent& render, const AssetManager& assets)
{
    auto ids = assets.loadedIds<MeshAsset>();
    ids.insert(ids.end(), {BuiltinAssets::Mesh::cube, BuiltinAssets::Mesh::sphere,
                          BuiltinAssets::Mesh::plane, BuiltinAssets::Mesh::arrow});
    std::ranges::sort(ids, {}, &MeshAsset::ID::value);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    bool edited = false;
    if (!ImGui::BeginCombo("Mesh", render.meshId.c_str())) return false;
    for (const auto& id : ids)
    {
        const bool selected = id == render.meshId;
        if (ImGui::Selectable(id.c_str(), selected) && !selected)
        {
            render.meshId = id;
            edited = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return edited;
}

bool drawMaterialFields(MaterialAsset& material)
{
    bool edited = false;
    glm::vec4 baseColor = material.baseColorFactor.value_or(glm::vec4{1.0f});
    if (ImGui::ColorEdit4("Base Color", glm::value_ptr(baseColor)))
    {
        material.baseColorFactor = baseColor;
        edited = true;
    }

    const auto drawFactor = [&](const char* label, std::optional<float>& field, float defaultValue)
    {
        float value = field.value_or(defaultValue);
        if (!ImGui::DragFloat(label, &value, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
            return false;

        field = value;
        return true;
    };
    edited = drawFactor("Metallic", material.metallic, 0.0f) || edited;
    edited = drawFactor("Roughness", material.roughness, 1.0f) || edited;
    edited = drawFactor("AO", material.ao, 1.0f) || edited;
    ImGui::Text("Albedo: %s", material.baseColorTexture ? material.baseColorTexture->textureID.c_str() : "");
    return edited;
}
}

ComponentDrawResult drawFields(RenderComponent& render)
{
    ComponentDrawResult result;
    const auto* assets = context().assetManager;
    if (!assets)
    {
        ImGui::TextDisabled("Assets unavailable");
        return result;
    }

    result.resourcesChanged = drawMeshSelection(render, *assets);
    result.edited = result.resourcesChanged;
    const auto* mesh = assets->find<MeshAsset>(render.meshId);
    if (!mesh)
    {
        ImGui::TextDisabled("Mesh description not loaded");
        return result;
    }

    ImGui::SeparatorText("Materials");
    for (size_t index = 0; index < mesh->submeshes.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen, "Submesh %zu", index))
        {
            const auto& materialId = mesh->submeshes[index].materialId;
            const auto* stock = assets->find<MaterialAsset>(materialId);
            ImGui::Text("Id: %s", materialId.c_str());
            if (stock)
            {
                const int submeshIndex = static_cast<int>(index);
                if (render.materialOverrides.contains(submeshIndex) && ImGui::Button("Reset overrides"))
                {
                    render.materialOverrides.erase(submeshIndex);
                    result.edited = true;
                }

                MaterialAsset preview = *stock;
                if (const auto found = render.materialOverrides.find(submeshIndex);
                    found != render.materialOverrides.end())
                    applyOptionalFields(preview, found->second);

                if (drawMaterialFields(preview))
                {
                    auto patch = materialOverrideDiff(*stock, preview);
                    if (hasMaterialOverrides(patch)) render.materialOverrides[submeshIndex] = std::move(patch);
                    else render.materialOverrides.erase(submeshIndex);
                    result.edited = true;
                }
            }
            else ImGui::TextDisabled("Material description not loaded");

            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    return result;
}
}

ComponentDrawResult drawComponent(Component& component)
{
    struct Drawer
    {
        const char* name;
        ComponentDrawResult (*draw)(Component&);
    };
    static const auto drawers = []
    {
        std::unordered_map<std::type_index, Drawer> result;
        forEachComponentType([&]<class T>(const char* name)
        {
            result.emplace(typeid(T), Drawer{name, [](Component& value)
            {
                return component_draw::drawFields(static_cast<T&>(value));
            }});
        });
        return result;
    }();

    const auto found = drawers.find(typeid(component));
    if (found == drawers.end())
    {
        ImGui::TextDisabled("Component has no registered inspector");
        return {};
    }

    ComponentDrawResult result;
    ImGui::PushID(&component);
    if (ImGui::CollapsingHeader(found->second.name, ImGuiTreeNodeFlags_DefaultOpen))
        result = found->second.draw(component);
    ImGui::PopID();
    return result;
}
