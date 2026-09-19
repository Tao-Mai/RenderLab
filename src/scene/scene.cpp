#include "scene/scene.h"

#include <fstream>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>
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

    nlohmann::json writeVec3(const glm::vec3 &value)
    {
        return nlohmann::json::array({value.x, value.y, value.z});
    }
}

glm::mat4 Transform::matrix() const
{
    glm::mat4 result = glm::translate(glm::mat4{1.0f}, position);
    result = glm::rotate(result, glm::radians(rotation.z), {0.0f, 0.0f, 1.0f});
    result = glm::rotate(result, glm::radians(rotation.y), {0.0f, 1.0f, 0.0f});
    result = glm::rotate(result, glm::radians(rotation.x), {1.0f, 0.0f, 0.0f});
    return glm::scale(result, scale);
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
    }

    for (const auto &objectJson : json.at("objects"))
    {
        SceneObject object;
        object.name = objectJson.value("name", "Object");
        object.mesh = objectJson.at("mesh").get<std::string>();
        if (const auto transform = objectJson.find("transform");
            transform != objectJson.end())
        {
            object.transform.position = readVec3(
                transform->value("position", nlohmann::json{}), object.transform.position);
            object.transform.rotation = readVec3(
                transform->value("rotation", nlohmann::json{}), object.transform.rotation);
            object.transform.scale = readVec3(
                transform->value("scale", nlohmann::json{}), object.transform.scale);
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
        }},
        {"objects", nlohmann::json::array()},
    };

    for (const SceneObject &object : objects)
    {
        json["objects"].push_back({
            {"name", object.name},
            {"mesh", object.mesh},
            {"transform", {
                {"position", writeVec3(object.transform.position)},
                {"rotation", writeVec3(object.transform.rotation)},
                {"scale", writeVec3(object.transform.scale)},
            }},
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
