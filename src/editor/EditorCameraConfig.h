#pragma once

#include <glm/vec3.hpp>

struct EditorCameraConfig
{
    glm::vec3 position{0.0f};
    float     yaw              = -90.0f;
    float     pitch            = 0.0f;
    float     fieldOfView      = 45.0f;
    float     nearPlane        = 0.1f;
    float     farPlane         = 100.0f;
    float     mouseSensitivity = 0.12f;
    float     speed            = 3.0f;
    float     sprintMultiplier = 3.0f;
};
