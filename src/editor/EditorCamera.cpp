#include "editor/EditorCamera.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Window.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

void EditorCamera::resetNavigation()
{
    if (looking && context().window)
        context().window->setCursorCaptured(false);
    looking        = false;
    freeMovement   = false;
    inputEnabled   = true;
    previousMouseX = 0.0;
    previousMouseY = 0.0;
}

void EditorCamera::tick(float deltaTime)
{
    DCHECK(context().inputManager && context().window);
    auto&       window = *context().window;
    const auto& input  = *context().inputManager;

    if (inputEnabled && input.get(Command::ToggleFreeMovement))
        freeMovement = !freeMovement;
    const bool navigationRequested = freeMovement || (input.get(Command::Navigate) && (inputEnabled || looking));
    if (navigationRequested != looking)
    {
        looking = navigationRequested;
        window.setCursorCaptured(looking);
        if (looking)
        {
            const auto [x, y] = window.cursorPosition();
            previousMouseX    = x;
            previousMouseY    = y;
        }
    }
    if (!looking)
    {
        window.consumeScrollOffset();
        return;
    }

    const auto [x, y] = window.cursorPosition();
    data.yaw += static_cast<float>(x - previousMouseX) * data.mouseSensitivity;
    data.pitch = std::clamp(data.pitch - static_cast<float>(y - previousMouseY) * data.mouseSensitivity, -89.0f, 89.0f);
    previousMouseX = x;
    previousMouseY = y;
    data.fieldOfView =
        std::clamp(data.fieldOfView - static_cast<float>(window.consumeScrollOffset()) * 2.0f, 20.0f, 90.0f);

    glm::vec3 direction{0.0f};
    if (input.get(Command::MoveForward))
        direction += forward();
    if (input.get(Command::MoveBackward))
        direction -= forward();
    if (input.get(Command::MoveRight))
        direction += right();
    if (input.get(Command::MoveLeft))
        direction -= right();
    if (input.get(Command::MoveUp))
        direction += up();
    if (input.get(Command::MoveDown))
        direction -= up();
    if (glm::dot(direction, direction) == 0.0f)
        return;

    const float speed = data.speed * (input.get(Command::Sprint) ? data.sprintMultiplier : 1.0f);
    data.position += glm::normalize(direction) * speed * std::clamp(deltaTime, 0.0f, 0.1f);
}

glm::vec3 EditorCamera::forward() const
{
    const float yaw   = glm::radians(data.yaw);
    const float pitch = glm::radians(data.pitch);
    return glm::normalize(glm::vec3{std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
}

glm::vec3 EditorCamera::right() const
{
    const float yaw = glm::radians(data.yaw);
    return {-std::sin(yaw), 0.0f, std::cos(yaw)};
}

glm::vec3 EditorCamera::up() const { return glm::normalize(glm::cross(right(), forward())); }

glm::mat4 EditorCamera::viewMatrix() const { return glm::lookAt(data.position, data.position + forward(), up()); }

glm::mat4 EditorCamera::projectionMatrix(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(glm::radians(data.fieldOfView), aspectRatio, data.nearPlane, data.farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}
