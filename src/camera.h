#pragma once

#include "scene/scene.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Window;

class Camera
{
  public:
    void configure(const SceneCamera &settings);
    void update(Window &window, float deltaTime);

    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const;

  private:
    glm::vec3 position{0.0f, 1.5f, 6.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fieldOfView = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float movementSpeed = 3.0f;
    float mouseSensitivity = 0.12f;
    bool looking = false;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;

    [[nodiscard]] glm::vec3 forward() const;
};
