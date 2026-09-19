#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct Transform
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};

    [[nodiscard]] glm::mat4 matrix() const;
};

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
};

class Scene
{
  public:
    [[nodiscard]] static Scene load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;

    SceneCamera camera;
    std::vector<SceneObject> objects;
};
