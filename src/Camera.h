#pragma once

#include "ecs/Camera.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Window;

class Camera
{
  public:
    void configure(const ecs::Camera& settings);
    void update(Window& window, float deltaTime, bool allowModeToggle);

    [[nodiscard]] bool isFreeMovementActive() const;
    [[nodiscard]] bool isNavigationActive() const;
    [[nodiscard]] const glm::vec3& worldPosition() const;
    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const;
    [[nodiscard]] ecs::Camera& component() noexcept;
    [[nodiscard]] const ecs::Camera& component() const noexcept;

  private:
    ecs::Camera data;
    bool looking = false;
    bool freeMovement = false;
    bool vWasPressed = false;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;

    [[nodiscard]] glm::vec3 forward() const;
};
