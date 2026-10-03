#pragma once

#include "scene/Component.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class CameraComponent : public Component
{
public:
    [[=ReflectField{}]] float yaw = -90.0f;
    [[=ReflectField{}]] float pitch = 0.0f;
    [[=ReflectField{}]] float fieldOfView = 45.0f;
    [[=ReflectField{}]] float nearPlane = 0.1f;
    [[=ReflectField{}]] float farPlane = 100.0f;
    [[=ReflectField{}]] float mouseSensitivity = 0.12f;

    void init(Actor* actor) override;
    void tick(float deltaTime) override;
    void setInputEnabled(bool enabled);
    void resetNavigation();

    [[nodiscard]] bool isFreeMovementActive() const;
    [[nodiscard]] bool isNavigationActive() const;
    [[nodiscard]] const glm::vec3& worldPosition() const;
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;
    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const;

private:
    bool inputEnabled = true;
    bool looking = false;
    bool freeMovement = false;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;
};
