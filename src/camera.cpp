#include "camera.h"

#include "window.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Camera::configure(const SceneCameraDesc &settings)
{
    position = settings.position;
    worldUp = settings.up;
    fieldOfView = settings.fieldOfView;
    nearPlane = settings.nearPlane;
    farPlane = settings.farPlane;
    movementSpeed = settings.movementSpeed;
    sprintMultiplier = settings.sprintMultiplier;

    const glm::vec3 direction = glm::normalize(settings.target - settings.position);
    pitch = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    yaw = glm::degrees(std::atan2(direction.z, direction.x));
    looking = false;
    freeMovement = false;
    vWasPressed = false;
}

void Camera::update(Window &window, float deltaTime, bool allowModeToggle)
{
    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    const bool vPressed = window.keyPressed(GLFW_KEY_V);
    if (allowModeToggle && vPressed && !vWasPressed)
    {
        freeMovement = !freeMovement;
    }
    vWasPressed = vPressed;

    const bool rightMousePressed = window.mouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
    const bool navigationRequested = freeMovement ||
        (rightMousePressed && (allowModeToggle || looking));
    if (navigationRequested != looking)
    {
        looking = navigationRequested;
        window.setCursorCaptured(looking);
        if (looking)
        {
            const auto [x, y] = window.cursorPosition();
            previousMouseX = x;
            previousMouseY = y;
        }
    }

    if (looking)
    {
        const auto [x, y] = window.cursorPosition();
        yaw += static_cast<float>(x - previousMouseX) * mouseSensitivity;
        pitch -= static_cast<float>(y - previousMouseY) * mouseSensitivity;
        pitch = std::clamp(pitch, -89.0f, 89.0f);
        previousMouseX = x;
        previousMouseY = y;
    }

    if (!looking)
    {
        window.consumeScrollOffset();
        return;
    }

    fieldOfView = std::clamp(
        fieldOfView - static_cast<float>(window.consumeScrollOffset()) * 2.0f,
        20.0f,
        90.0f);

    const glm::vec3 front = forward();
    const glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
    glm::vec3 movement{0.0f};
    if (window.keyPressed(GLFW_KEY_W)) movement += front;
    if (window.keyPressed(GLFW_KEY_S)) movement -= front;
    if (window.keyPressed(GLFW_KEY_D)) movement += right;
    if (window.keyPressed(GLFW_KEY_A)) movement -= right;
    if (window.keyPressed(GLFW_KEY_E)) movement += worldUp;
    if (window.keyPressed(GLFW_KEY_Q)) movement -= worldUp;

    if (glm::dot(movement, movement) > 0.0f)
    {
        float speed = movementSpeed;
        if (window.keyPressed(GLFW_KEY_LEFT_SHIFT))
        {
            speed *= sprintMultiplier;
        }
        position += glm::normalize(movement) * speed * deltaTime;
    }
}

bool Camera::isFreeMovementActive() const
{
    return freeMovement;
}

bool Camera::isNavigationActive() const
{
    return looking;
}

const glm::vec3 &Camera::worldPosition() const
{
    return position;
}

glm::mat4 Camera::viewMatrix() const
{
    return glm::lookAt(position, position + forward(), worldUp);
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(
        glm::radians(fieldOfView), aspectRatio, nearPlane, farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}

glm::vec3 Camera::forward() const
{
    const float yawRadians = glm::radians(yaw);
    const float pitchRadians = glm::radians(pitch);
    return glm::normalize(glm::vec3{
        std::cos(yawRadians) * std::cos(pitchRadians),
        std::sin(pitchRadians),
        std::sin(yawRadians) * std::cos(pitchRadians),
    });
}
