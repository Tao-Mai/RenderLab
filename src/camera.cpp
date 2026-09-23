#include "camera.h"

#include "core/window.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Camera::configure(const SceneCameraDesc& settings)
{
    data.position = settings.position;
    data.worldUp = settings.up;
    data.fieldOfView = settings.fieldOfView;
    data.nearPlane = settings.nearPlane;
    data.farPlane = settings.farPlane;
    data.movementSpeed = settings.movementSpeed;
    data.sprintMultiplier = settings.sprintMultiplier;

    const glm::vec3 direction = glm::normalize(settings.target - settings.position);
    data.pitch = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    data.yaw = glm::degrees(std::atan2(direction.z, direction.x));
    data.looking = false;
    data.freeMovement = false;
    data.vWasPressed = false;
}

void Camera::update(Window& window, float deltaTime, bool allowModeToggle)
{
    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    const bool vPressed = window.keyPressed(GLFW_KEY_V);
    if (allowModeToggle && vPressed && !data.vWasPressed)
    {
        data.freeMovement = !data.freeMovement;
    }
    data.vWasPressed = vPressed;

    const bool rightMousePressed = window.mouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
    const bool navigationRequested = data.freeMovement ||
        (rightMousePressed && (allowModeToggle || data.looking));
    if (navigationRequested != data.looking)
    {
        data.looking = navigationRequested;
        window.setCursorCaptured(data.looking);
        if (data.looking)
        {
            const auto [x, y] = window.cursorPosition();
            data.previousMouseX = x;
            data.previousMouseY = y;
        }
    }

    if (data.looking)
    {
        const auto [x, y] = window.cursorPosition();
        data.yaw += static_cast<float>(x - data.previousMouseX) * data.mouseSensitivity;
        data.pitch -= static_cast<float>(y - data.previousMouseY) * data.mouseSensitivity;
        data.pitch = std::clamp(data.pitch, -89.0f, 89.0f);
        data.previousMouseX = x;
        data.previousMouseY = y;
    }

    if (!data.looking)
    {
        window.consumeScrollOffset();
        return;
    }

    data.fieldOfView = std::clamp(
        data.fieldOfView - static_cast<float>(window.consumeScrollOffset()) * 2.0f,
        20.0f,
        90.0f);

    const glm::vec3 front = forward();
    const glm::vec3 right = glm::normalize(glm::cross(front, data.worldUp));
    glm::vec3 movement{0.0f};
    if (window.keyPressed(GLFW_KEY_W)) movement += front;
    if (window.keyPressed(GLFW_KEY_S)) movement -= front;
    if (window.keyPressed(GLFW_KEY_D)) movement += right;
    if (window.keyPressed(GLFW_KEY_A)) movement -= right;
    if (window.keyPressed(GLFW_KEY_E)) movement += data.worldUp;
    if (window.keyPressed(GLFW_KEY_Q)) movement -= data.worldUp;

    if (glm::dot(movement, movement) > 0.0f)
    {
        float speed = data.movementSpeed;
        if (window.keyPressed(GLFW_KEY_LEFT_SHIFT))
        {
            speed *= data.sprintMultiplier;
        }
        data.position += glm::normalize(movement) * speed * deltaTime;
    }
}

bool Camera::isFreeMovementActive() const
{
    return data.freeMovement;
}

bool Camera::isNavigationActive() const
{
    return data.looking;
}

const glm::vec3& Camera::worldPosition() const
{
    return data.position;
}

glm::mat4 Camera::viewMatrix() const
{
    return glm::lookAt(data.position, data.position + forward(), data.worldUp);
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(
        glm::radians(data.fieldOfView), aspectRatio, data.nearPlane, data.farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}

ecs::Camera& Camera::component() noexcept
{
    return data;
}

const ecs::Camera& Camera::component() const noexcept
{
    return data;
}

glm::vec3 Camera::forward() const
{
    const float yawRadians = glm::radians(data.yaw);
    const float pitchRadians = glm::radians(data.pitch);
    return glm::normalize(glm::vec3{
        std::cos(yawRadians) * std::cos(pitchRadians),
        std::sin(pitchRadians),
        std::sin(yawRadians) * std::cos(pitchRadians),
    });
}
