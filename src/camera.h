#pragma once

#include "asset/asset_desc.h"
#include "ecs/camera.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Window;

class Camera
{
  public:
    void configure(const SceneCameraDesc& settings);
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

    [[nodiscard]] glm::vec3 forward() const;
};
