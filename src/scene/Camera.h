#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct Camera
{
    glm::vec3 position { 0.0f };

    float yaw = -90.0f;

    float pitch = 0.0f;

    float fieldOfView = 45.0f;

    float lensRadius = 0.0f;

    float focalDistance = 1e6f;

    float nearPlane = 0.1f;

    float farPlane = 100.0f;

    float mouseSensitivity = 0.12f;

    float speed = 3.0f;

    float sprintMultiplier = 3.0f;

    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;
    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const;

    bool operator==(const Camera&) const = default;
};
