#include "editor/component_draw.h"

#include "asset/asset_desc.h"
#include "asset/asset_manager.h"
#include "core/context.h"
#include "core/logger.h"
#include "ecs/light.h"
#include "ecs/render.h"
#include "ecs/transform.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include <entt/meta/resolve.hpp>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace
{
[[nodiscard]] bool drawTransform(ecs::Transform& transform)
{
    bool edited = ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.05f);
    glm::vec3 rotationEulerDegrees = transform.rotationEulerDegrees();
    if (ImGui::DragFloat3(
            "Rotation",
            glm::value_ptr(rotationEulerDegrees),
            0.25f,
            0.0f,
            0.0f,
            "%.1f deg"))
    {
        transform.setRotationEulerDegrees(rotationEulerDegrees);
        edited = true;
    }
    if (ImGui::DragFloat3("Scale", glm::value_ptr(transform.scale), 0.01f))
    {
        transform.scale = glm::max(transform.scale, glm::vec3{0.001f});
        edited = true;
    }
    return edited;
}

[[nodiscard]] bool drawLight(ecs::Light& light)
{
    bool edited = false;
    static constexpr const char* typeNames[] = {
        "Point",
        "Directional",
        "Rect Area",
        "Spot",
    };
    int type = static_cast<int>(light.type);
    if (ImGui::Combo("Type", &type, typeNames, IM_ARRAYSIZE(typeNames)))
    {
        light.type = static_cast<ecs::Light::Type>(type);
        edited = true;
    }

    edited = ImGui::ColorEdit3("Color", glm::value_ptr(light.color)) || edited;
    if (ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100000.0f))
    {
        light.intensity = std::max(light.intensity, 0.0f);
        edited = true;
    }

    if (light.type == ecs::Light::Type::Point || light.type == ecs::Light::Type::Spot)
    {
        if (ImGui::DragFloat("Range", &light.range, 0.1f, 0.01f, 100000.0f))
        {
            light.range = std::max(light.range, 0.01f);
            edited = true;
        }
    }
    if (light.type == ecs::Light::Type::Spot)
    {
        float innerAngle = glm::degrees(std::acos(std::clamp(light.cosInner, -1.0f, 1.0f)));
        float outerAngle = glm::degrees(std::acos(std::clamp(light.cosOuter, -1.0f, 1.0f)));
        if (ImGui::DragFloat("Inner Angle", &innerAngle, 0.25f, 0.0f, 89.0f, "%.1f deg"))
        {
            innerAngle = std::clamp(innerAngle, 0.0f, outerAngle);
            light.cosInner = std::cos(glm::radians(innerAngle));
            edited = true;
        }
        if (ImGui::DragFloat("Outer Angle", &outerAngle, 0.25f, 0.0f, 89.0f, "%.1f deg"))
        {
            outerAngle = std::clamp(outerAngle, innerAngle, 89.0f);
            light.cosOuter = std::cos(glm::radians(outerAngle));
            edited = true;
        }
    }
    if (light.type == ecs::Light::Type::RectArea)
    {
        if (ImGui::DragFloat2(
                "Area Size", glm::value_ptr(light.areaSize), 0.05f, 0.01f, 100000.0f))
        {
            light.areaSize = glm::max(light.areaSize, glm::vec2{0.01f});
            edited = true;
        }
    }
    edited = ImGui::Checkbox("Enabled", &light.enabled) || edited;
    edited = ImGui::Checkbox("Cast Shadow", &light.castShadow) || edited;
    return edited;
}

bool editAssetId(const char* label, AssetId& id)
{
    std::array<char, 256> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", id.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size()))
    {
        id = buffer.data();
        return true;
    }
    return false;
}

MaterialDesc materialPreview(const AssetId& materialId)
{
    if (isBuiltin<MaterialDesc>(materialId))
    {
        MaterialDesc material{};
        material.id = materialId;
        material.metallic = 0.0f;
        material.roughness = 0.4f;
        material.baseColorFactor = glm::vec4{1.0f};
        material.baseColorTexture = TextureDesc::white;
        return material;
    }
    if (context().assetManager != nullptr)
    {
        if (const MaterialDesc* material = context().assetManager->findDesc<MaterialDesc>(materialId))
        {
            return *material;
        }
    }
    return {};
}

[[nodiscard]] bool drawMaterialFields(MaterialDesc& material, bool editable)
{
    bool edited = false;
    ImGui::BeginDisabled(!editable);
    glm::vec4 baseColor = material.baseColorFactor.value_or(glm::vec4{1.0f});
    if (ImGui::ColorEdit4("Base Color", glm::value_ptr(baseColor)))
    {
        material.baseColorFactor = baseColor;
        edited = true;
    }
    float metallic = material.metallic.value_or(0.0f);
    if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f))
    {
        material.metallic = metallic;
        edited = true;
    }
    float roughness = material.roughness.value_or(1.0f);
    if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f))
    {
        material.roughness = roughness;
        edited = true;
    }
    float ao = material.ao.value_or(1.0f);
    if (ImGui::DragFloat("AO", &ao, 0.01f, 0.0f, 1.0f))
    {
        material.ao = ao;
        edited = true;
    }
    ImGui::EndDisabled();
    ImGui::Text(
        "Albedo: %s",
        material.baseColorTexture.value_or(AssetId{}).c_str());
    return edited;
}

[[nodiscard]] MaterialDesc materialPatchFromDiff(
    const MaterialDesc& stock, const MaterialDesc& edited)
{
    MaterialDesc patch{};
    if (edited.baseColorFactor != stock.baseColorFactor)
    {
        patch.baseColorFactor = edited.baseColorFactor;
    }
    if (edited.metallic != stock.metallic)
    {
        patch.metallic = edited.metallic;
    }
    if (edited.roughness != stock.roughness)
    {
        patch.roughness = edited.roughness;
    }
    if (edited.ao != stock.ao)
    {
        patch.ao = edited.ao;
    }
    return patch;
}

[[nodiscard]] bool materialPatchEmpty(const MaterialDesc& patch)
{
    return !patch.baseColorFactor && !patch.metallic && !patch.roughness && !patch.ao &&
        !patch.emissive && !patch.normalScale && !patch.baseColorTexture &&
        !patch.normalTexture && !patch.metallicTexture && !patch.roughnessTexture &&
        !patch.aoTexture && !patch.emissiveTexture && !patch.alphaMode &&
        !patch.alphaCutoff && !patch.doubleSided;
}

void drawMaterial(const AssetId& materialId)
{
    ImGui::Text("Id: %s", materialId.c_str());
    if (materialId.empty())
    {
        return;
    }

    MaterialDesc material = materialPreview(materialId);
    if (material.id.empty() && !isBuiltin<MaterialDesc>(materialId))
    {
        ImGui::TextDisabled("desc not loaded");
        return;
    }
    if (isBuiltin<MaterialDesc>(materialId))
    {
        ImGui::TextDisabled("builtin");
    }

    (void)drawMaterialFields(material, false);
}

[[nodiscard]] bool drawRender(ecs::Render& render)
{
    bool edited = editAssetId("Mesh", render.meshId);

    ImGui::Spacing();
    ImGui::TextUnformatted("Materials");
    ImGui::Separator();

    if (render.meshId.empty())
    {
        ImGui::TextDisabled("no mesh");
        return edited;
    }

    auto drawSubmesh = [&](int index, const AssetId& materialId) {
        ImGui::PushID(index);
        const bool open = ImGui::TreeNode("Material", "Submesh %d", index);
        if (open)
        {
            const MaterialDesc stock = materialPreview(materialId);
            MaterialDesc preview = stock;
            if (const auto it = render.materialOverrides.find(index);
                it != render.materialOverrides.end())
            {
                applyMaterialFields(preview, it->second);
            }

            if (drawMaterialFields(preview, true))
            {
                edited = true;
            }
            const MaterialDesc patch = materialPatchFromDiff(stock, preview);
            if (materialPatchEmpty(patch))
            {
                render.materialOverrides.erase(index);
            }
            else
            {
                render.materialOverrides[index] = patch;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    if (isBuiltin<MeshDesc>(render.meshId))
    {
        drawSubmesh(0, MaterialDesc::white);
        return edited;
    }

    if (context().assetManager == nullptr)
    {
        ImGui::TextDisabled("assets unavailable");
        return edited;
    }

    const MeshDesc* mesh = context().assetManager->findDesc<MeshDesc>(render.meshId);
    if (mesh == nullptr)
    {
        ImGui::TextDisabled("mesh desc not loaded");
        return edited;
    }
    if (mesh->submeshes.empty())
    {
        ImGui::TextDisabled("no submeshes");
        return edited;
    }

    for (size_t index = 0; index < mesh->submeshes.size(); ++index)
    {
        drawSubmesh(static_cast<int>(index), mesh->submeshes[index].materialId);
    }
    return edited;
}

bool drawMetaValue(const char* label, entt::meta_any& value);

[[nodiscard]] bool drawMetaFields(entt::meta_any& component)
{
    bool edited = false;
    const entt::meta_type type = component.type();
    for (auto&& [id, data] : type.data())
    {
        (void)id;
        const char* name = data.name();
        if (name == nullptr)
        {
            continue;
        }

        entt::meta_any field = data.get(component);
        if (!field)
        {
            continue;
        }

        ImGui::PushID(name);
        if (drawMetaValue(name, field))
        {
            data.set(component, field);
            edited = true;
        }
        ImGui::PopID();
    }
    return edited;
}

bool drawMetaValue(const char* label, entt::meta_any& value)
{
    const entt::meta_type type = value.type();

    if (type == entt::resolve<bool>())
    {
        auto* v = value.try_cast<bool>();
        CHECK(v != nullptr, "meta value is not bool");
        return ImGui::Checkbox(label, v);
    }
    if (type == entt::resolve<float>())
    {
        auto* v = value.try_cast<float>();
        CHECK(v != nullptr, "meta value is not float");
        return ImGui::DragFloat(label, v, 0.05f);
    }
    if (type == entt::resolve<double>())
    {
        auto* v = value.try_cast<double>();
        CHECK(v != nullptr, "meta value is not double");
        float asFloat = static_cast<float>(*v);
        if (ImGui::DragFloat(label, &asFloat, 0.05f))
        {
            *v = static_cast<double>(asFloat);
            return true;
        }
        return false;
    }
    if (type == entt::resolve<int>())
    {
        auto* v = value.try_cast<int>();
        CHECK(v != nullptr, "meta value is not int");
        return ImGui::DragInt(label, v);
    }
    if (type == entt::resolve<std::int64_t>())
    {
        auto* v = value.try_cast<std::int64_t>();
        CHECK(v != nullptr, "meta value is not int64");
        int asInt = static_cast<int>(*v);
        if (ImGui::DragInt(label, &asInt))
        {
            *v = static_cast<std::int64_t>(asInt);
            return true;
        }
        return false;
    }
    if (type == entt::resolve<std::uint32_t>())
    {
        auto* v = value.try_cast<std::uint32_t>();
        CHECK(v != nullptr, "meta value is not uint32");
        int asInt = static_cast<int>(*v);
        if (ImGui::DragInt(label, &asInt, 1.0f, 0))
        {
            *v = static_cast<std::uint32_t>(std::max(asInt, 0));
            return true;
        }
        return false;
    }
    if (type == entt::resolve<std::string>())
    {
        auto* v = value.try_cast<std::string>();
        CHECK(v != nullptr, "meta value is not string");
        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", v->c_str());
        if (ImGui::InputText(label, buffer.data(), buffer.size()))
        {
            *v = buffer.data();
            return true;
        }
        return false;
    }
    if (type == entt::resolve<glm::vec2>())
    {
        auto* v = value.try_cast<glm::vec2>();
        CHECK(v != nullptr, "meta value is not vec2");
        return ImGui::DragFloat2(label, glm::value_ptr(*v), 0.05f);
    }
    if (type == entt::resolve<glm::vec3>())
    {
        auto* v = value.try_cast<glm::vec3>();
        CHECK(v != nullptr, "meta value is not vec3");
        return ImGui::DragFloat3(label, glm::value_ptr(*v), 0.05f);
    }
    if (type == entt::resolve<glm::vec4>())
    {
        auto* v = value.try_cast<glm::vec4>();
        CHECK(v != nullptr, "meta value is not vec4");
        return ImGui::DragFloat4(label, glm::value_ptr(*v), 0.05f);
    }
    if (type == entt::resolve<glm::quat>())
    {
        auto* v = value.try_cast<glm::quat>();
        CHECK(v != nullptr, "meta value is not quat");
        return ImGui::DragFloat4(label, glm::value_ptr(*v), 0.01f);
    }

    if (type.data().begin() != type.data().end())
    {
        if (ImGui::TreeNode(label))
        {
            const bool edited = drawMetaFields(value);
            ImGui::TreePop();
            return edited;
        }
        return false;
    }

    ImGui::Text("%s: <unsupported>", label);
    return false;
}
}

bool drawComponent(std::string_view typeName, entt::meta_any& component)
{
    if (!component)
    {
        return false;
    }

    ImGui::TextUnformatted(typeName.data(), typeName.data() + typeName.size());
    ImGui::Separator();

    if (typeName == "Transform")
    {
        auto* transform = component.try_cast<ecs::Transform>();
        CHECK(transform != nullptr, "component '{}' is not Transform", typeName);
        return drawTransform(*transform);
    }
    if (typeName == "Light")
    {
        auto* light = component.try_cast<ecs::Light>();
        CHECK(light != nullptr, "component '{}' is not Light", typeName);
        return drawLight(*light);
    }
    if (typeName == "Render")
    {
        auto* render = component.try_cast<ecs::Render>();
        CHECK(render != nullptr, "component '{}' is not Render", typeName);
        return drawRender(*render);
    }

    return drawMetaFields(component);
}
