#include "scene/scene.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

namespace
{
    glm::vec3 readVec3(const nlohmann::json &value, const glm::vec3 &fallback)
    {
        if (!value.is_array() || value.size() != 3)
        {
            return fallback;
        }
        return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    }

    glm::vec2 readVec2(const nlohmann::json &value, const glm::vec2 &fallback)
    {
        if (!value.is_array() || value.size() != 2)
        {
            return fallback;
        }
        return {value[0].get<float>(), value[1].get<float>()};
    }

    nlohmann::json writeVec3(const glm::vec3 &value)
    {
        return nlohmann::json::array({value.x, value.y, value.z});
    }

    nlohmann::json writeVec2(const glm::vec2 &value)
    {
        return nlohmann::json::array({value.x, value.y});
    }

    Light::Type readLightType(const std::string &type)
    {
        if (type == "point") return Light::Type::Point;
        if (type == "directional") return Light::Type::Directional;
        if (type == "rectArea") return Light::Type::RectArea;
        if (type == "spot") return Light::Type::Spot;
        throw std::runtime_error("unsupported light type: " + type);
    }

    const char *writeLightType(Light::Type type)
    {
        switch (type)
        {
        case Light::Type::Point: return "point";
        case Light::Type::Directional: return "directional";
        case Light::Type::RectArea: return "rectArea";
        case Light::Type::Spot: return "spot";
        }
        throw std::runtime_error("unsupported light type");
    }

    void validateLight(Light &light)
    {
        if (light.intensity < 0.0f)
        {
            throw std::runtime_error("light intensity cannot be negative: " + light.name);
        }
        if ((light.type == Light::Type::Point || light.type == Light::Type::Spot) &&
            light.range <= 0.0f)
        {
            throw std::runtime_error("light range must be greater than zero: " + light.name);
        }
        if (light.type != Light::Type::Point)
        {
            if (glm::dot(light.direction, light.direction) < 0.000001f)
            {
                throw std::runtime_error("light direction cannot be zero: " + light.name);
            }
            light.direction = glm::normalize(light.direction);
        }
        if (light.type == Light::Type::Spot)
        {
            light.cosInner = std::clamp(light.cosInner, -1.0f, 1.0f);
            light.cosOuter = std::clamp(light.cosOuter, -1.0f, 1.0f);
            if (light.cosInner < light.cosOuter)
            {
                throw std::runtime_error(
                    "spot light inner angle must not exceed outer angle: " + light.name);
            }
        }
        if (light.type == Light::Type::RectArea &&
            (light.areaSize.x <= 0.0f || light.areaSize.y <= 0.0f))
        {
            throw std::runtime_error("area light size must be greater than zero: " + light.name);
        }
    }

    Light readLight(const nlohmann::json &json)
    {
        Light light;
        light.type = readLightType(json.at("type").get<std::string>());
        light.name = json.value("name", light.name);
        light.color = readVec3(json.value("color", nlohmann::json{}), light.color);
        light.position = readVec3(
            json.value("position", nlohmann::json{}), light.position);
        light.direction = readVec3(
            json.value("direction", nlohmann::json{}), light.direction);
        light.intensity = json.value("intensity", light.intensity);
        light.range = json.value("range", light.range);
        light.cosInner = json.value("cosInner", light.cosInner);
        light.cosOuter = json.value("cosOuter", light.cosOuter);
        light.areaSize = readVec2(
            json.value("areaSize", nlohmann::json{}), light.areaSize);
        light.enabled = json.value("enabled", light.enabled);
        light.castShadow = json.value("castShadow", light.castShadow);
        validateLight(light);
        return light;
    }

    nlohmann::json writeLight(const Light &light)
    {
        nlohmann::json json{
            {"name", light.name},
            {"type", writeLightType(light.type)},
            {"color", writeVec3(light.color)},
            {"intensity", light.intensity},
            {"enabled", light.enabled},
            {"castShadow", light.castShadow},
        };

        switch (light.type)
        {
        case Light::Type::Point:
            json["position"] = writeVec3(light.position);
            json["range"] = light.range;
            break;
        case Light::Type::Directional:
            json["direction"] = writeVec3(light.direction);
            break;
        case Light::Type::RectArea:
            json["position"] = writeVec3(light.position);
            json["direction"] = writeVec3(light.direction);
            json["areaSize"] = writeVec2(light.areaSize);
            break;
        case Light::Type::Spot:
            json["position"] = writeVec3(light.position);
            json["direction"] = writeVec3(light.direction);
            json["range"] = light.range;
            json["cosInner"] = light.cosInner;
            json["cosOuter"] = light.cosOuter;
            break;
        }
        return json;
    }

    void readTransform(const nlohmann::json &json, Transform &transform)
    {
        transform.position = readVec3(
            json.value("position", nlohmann::json{}), transform.position);
        transform.setRotationEulerDegrees(readVec3(
            json.value("rotation", nlohmann::json{}),
            transform.rotationEulerDegrees()));
        transform.scale = readVec3(
            json.value("scale", nlohmann::json{}), transform.scale);
    }

    nlohmann::json writeTransform(const Transform &transform)
    {
        return {
            {"position", writeVec3(transform.position)},
            {"rotation", writeVec3(transform.rotationEulerDegrees())},
            {"scale", writeVec3(transform.scale)},
        };
    }
}

Scene Scene::load(const std::filesystem::path &path)
{
    std::ifstream stream(path);
    if (!stream)
    {
        throw std::runtime_error("failed to open scene: " + path.string());
    }

    nlohmann::json json;
    stream >> json;

    Scene scene;
    if (const auto camera = json.find("camera"); camera != json.end())
    {
        scene.camera.position = readVec3(camera->value("position", nlohmann::json{}),
                                         scene.camera.position);
        scene.camera.target = readVec3(camera->value("target", nlohmann::json{}),
                                       scene.camera.target);
        scene.camera.up = readVec3(camera->value("up", nlohmann::json{}), scene.camera.up);
        scene.camera.fieldOfView = camera->value("fieldOfView", scene.camera.fieldOfView);
        scene.camera.nearPlane = camera->value("nearPlane", scene.camera.nearPlane);
        scene.camera.farPlane = camera->value("farPlane", scene.camera.farPlane);
        scene.camera.movementSpeed = camera->value(
            "movementSpeed",
            scene.camera.movementSpeed);
        scene.camera.sprintMultiplier = camera->value(
            "sprintMultiplier",
            scene.camera.sprintMultiplier);

        if (scene.camera.movementSpeed <= 0.0f)
        {
            throw std::runtime_error("camera movement speed must be greater than zero");
        }
        if (scene.camera.sprintMultiplier < 1.0f)
        {
            throw std::runtime_error("camera sprint multiplier must be at least one");
        }
    }

    if (const auto lights = json.find("lights");
        lights != json.end() && lights->is_array())
    {
        for (const auto &lightJson : *lights)
        {
            scene.lights.push_back(readLight(lightJson));
        }
    }
    for (const auto &objectJson : json.at("objects"))
    {
        SceneObject object;
        object.name = objectJson.value("name", "Object");
        object.mesh = objectJson.at("mesh").get<std::string>();
        if (const auto transform = objectJson.find("transform");
            transform != objectJson.end())
        {
            readTransform(*transform, object.transform);
        }
        scene.objects.push_back(std::move(object));
    }
    return scene;
}

void Scene::save(const std::filesystem::path &path) const
{
    nlohmann::json json{
        {"version", 1},
        {"camera", {
            {"position", writeVec3(camera.position)},
            {"target", writeVec3(camera.target)},
            {"up", writeVec3(camera.up)},
            {"fieldOfView", camera.fieldOfView},
            {"nearPlane", camera.nearPlane},
            {"farPlane", camera.farPlane},
            {"movementSpeed", camera.movementSpeed},
            {"sprintMultiplier", camera.sprintMultiplier},
        }},
        {"lights", nlohmann::json::array()},
        {"objects", nlohmann::json::array()},
    };

    for (const Light &light : lights)
    {
        json["lights"].push_back(writeLight(light));
    }

    for (const SceneObject &object : objects)
    {
        json["objects"].push_back({
            {"name", object.name},
            {"mesh", object.mesh},
            {"transform", writeTransform(object.transform)},
        });
    }

    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream stream(path);
    if (!stream)
    {
        throw std::runtime_error("failed to save scene: " + path.string());
    }
    stream << json.dump(2) << '\n';
}
