#include "editor/EditorCamera.h"

#include "core/Context.h"
#include "core/Window.h"

#include <algorithm>
#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <tuple>
#include <utility>

void EditorCamera::setLooking(bool enabled)
{
    const auto& window = *context().window;
    window.setCursorCaptured(enabled);

    looking = enabled;
    if (looking)
        std::tie(previousMouseX, previousMouseY) = window.cursorPosition();
}

void EditorCamera::resetNavigation()
{
    if (looking)
        setLooking(false);

    freeMovement = false;
    freeMovementToggleRequested = false;
    inputEnabled = true;
    scrollOffset = 0.0;
}

void EditorCamera::tick(float deltaTime)
{
    const auto& window = *context().window;
    auto* handle = window.nativeHandle();
    const bool toggleFreeMovement = std::exchange(freeMovementToggleRequested, false);
    if (inputEnabled && toggleFreeMovement)
        freeMovement = !freeMovement;

    const bool rightMouseHeld = glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool navigationRequested = freeMovement || (rightMouseHeld && (inputEnabled || looking));
    if (navigationRequested != looking)
        setLooking(navigationRequested);

    const auto scroll = static_cast<float>(std::exchange(scrollOffset, 0.0));
    if (!looking)
        return;

    const auto [x, y] = window.cursorPosition();
    data.yaw += static_cast<float>(x - previousMouseX) * data.mouseSensitivity;
    data.pitch = std::clamp(data.pitch - static_cast<float>(y - previousMouseY) * data.mouseSensitivity, -89.0f, 89.0f);
    previousMouseX = x;
    previousMouseY = y;
    data.fieldOfView =
        std::clamp(data.fieldOfView - scroll * 2.0f, 20.0f, 90.0f);

    const auto pressed = [handle](int key) { return glfwGetKey(handle, key) == GLFW_PRESS; };
    glm::vec3 direction{0.0f};
    if (pressed(GLFW_KEY_W))
        direction += data.forward();
    if (pressed(GLFW_KEY_S))
        direction -= data.forward();
    if (pressed(GLFW_KEY_D))
        direction += data.right();
    if (pressed(GLFW_KEY_A))
        direction -= data.right();
    if (pressed(GLFW_KEY_E))
        direction += data.up();
    if (pressed(GLFW_KEY_Q))
        direction -= data.up();
    if (glm::dot(direction, direction) == 0.0f)
        return;

    const bool sprint = pressed(GLFW_KEY_LEFT_SHIFT) || pressed(GLFW_KEY_RIGHT_SHIFT);
    const float speed = data.speed * (sprint ? data.sprintMultiplier : 1.0f);
    data.position += glm::normalize(direction) * speed * std::clamp(deltaTime, 0.0f, 0.1f);
}
