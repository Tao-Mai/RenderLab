#pragma once

#include "scene/light.h"
#include "scene/transform.h"

#include <filesystem>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct SceneObject
{
    std::string name;
    std::string mesh;
    Transform transform;
};

struct SceneCamera
{
    glm::vec3 position{0.0f, 1.5f, 6.0f};
    glm::vec3 target{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fieldOfView = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float movementSpeed = 3.0f;
    float sprintMultiplier = 3.0f;
};

class Scene
{
  public:
    [[nodiscard]] static Scene load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;

    SceneCamera camera;
    std::vector<Light> lights;
    std::vector<SceneObject> objects;
};
