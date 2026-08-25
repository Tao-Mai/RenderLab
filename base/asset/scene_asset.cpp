#include "base/asset/scene_asset.h"
#include "base/asset/scene_asset_json.h"

#include "base/asset/json_util.h"
#include "base/asset/reflection/meta_json.h"

#include <glog/logging.h>
#include <nlohmann/json.hpp>

SceneAsset scene_asset_json::deserialize(const nlohmann::json& j)
{
    SceneAsset asset;
    if (const auto id_text = j.value("id", std::string{}); !id_text.empty())
    {
        if (const auto parsed = parse_asset_id(id_text))
            asset.id = *parsed;
        else
            LOG(WARNING) << "Invalid scene asset id: " << id_text;
    }

    asset.name        = j.value("name", std::string{});
    asset.clear_color = asset_json::read_vec3(j.value("clear_color", nlohmann::json::array()),
                                              asset.clear_color);
    asset.ambient =
        asset_json::read_vec3(j.value("ambient", nlohmann::json::array()), asset.ambient);

    if (j.contains("camera") && j["camera"].is_object())
    {
        SceneCameraConfig cam;
        const auto&       c = j["camera"];
        cam.position        = asset_json::read_vec3(c.value("position", nlohmann::json::array()),
                                                    cam.position);
        cam.target = asset_json::read_vec3(c.value("target", nlohmann::json::array()), cam.target);
        cam.fov    = c.value("fov", cam.fov);
        asset.camera = cam;
    }

    if (!j.contains("entities") || !j["entities"].is_array())
        return asset;

    for (const auto& entity_node : j["entities"])
    {
        EntityDesc entity;
        entity.name = entity_node.value("name", std::string{"entity"});

        if (!entity_node.contains("components") || !entity_node["components"].is_object())
        {
            asset.entities.push_back(std::move(entity));
            continue;
        }

        for (auto it = entity_node["components"].begin(); it != entity_node["components"].end();
             ++it)
        {
            const std::string type_name = it.key();
            entt::meta_any    component = meta_json::from_json(type_name, *it);
            if (!component)
            {
                LOG(WARNING) << "Skip component '" << type_name << "' on entity '" << entity.name
                             << "'";
                continue;
            }
            entity.components.emplace(type_name, std::move(component));
        }

        asset.entities.push_back(std::move(entity));
    }

    return asset;
}

nlohmann::json scene_asset_json::serialize(const SceneAsset& asset)
{
    nlohmann::json j = {
        {"id", format_asset_id(asset.id)},
        {"type", "scene"},
        {"name", asset.name},
        {"clear_color", asset_json::write_vec3(asset.clear_color)},
        {"ambient", asset_json::write_vec3(asset.ambient)},
    };

    if (asset.camera)
    {
        j["camera"] = {
            {"position", asset_json::write_vec3(asset.camera->position)},
            {"target", asset_json::write_vec3(asset.camera->target)},
            {"fov", asset.camera->fov},
        };
    }

    nlohmann::json entities = nlohmann::json::array();
    for (const EntityDesc& entity : asset.entities)
    {
        nlohmann::json components = nlohmann::json::object();
        for (const auto& [type_name, component] : entity.components)
            components[type_name] = meta_json::to_json(component);

        entities.push_back({
            {"name", entity.name},
            {"components", std::move(components)},
        });
    }
    j["entities"] = std::move(entities);
    return j;
}
