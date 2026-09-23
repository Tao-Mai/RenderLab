#pragma once

#include "ecs/component.h"

#include <glm/vec3.hpp>

namespace ecs
{
struct Camera
{
    glm::vec3 position{0.0f, 1.5f, 6.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fieldOfView = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float movementSpeed = 3.0f;
    float sprintMultiplier = 3.0f;
    float mouseSensitivity = 0.12f;
    bool looking = false;
    bool freeMovement = false;
    bool vWasPressed = false;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;
};

static_assert(Component<Camera>);
}
