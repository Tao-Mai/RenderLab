#pragma once

#include "ecs/component/CameraComponent.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ecs
{
class CameraSystem
{
public:
    void reset();
    [[nodiscard]] bool tick(bool allowModeToggle);

    [[nodiscard]] bool isFreeMovementActive() const;
    [[nodiscard]] bool isNavigationActive() const;
    [[nodiscard]] const glm::vec3& worldPosition() const;
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;
    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const;
    [[nodiscard]] const CameraComponent& component() const;

private:
    bool looking = false;
    bool freeMovement = false;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;
};
}
