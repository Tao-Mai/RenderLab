#include "editor/EditorCamera.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Window.h"

#include <algorithm>
#include <glm/geometric.hpp>

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

    const auto camera = data.sceneCamera();
    glm::vec3  direction{0.0f};
    if (input.get(Command::MoveForward))
        direction += camera.forward();
    if (input.get(Command::MoveBackward))
        direction -= camera.forward();
    if (input.get(Command::MoveRight))
        direction += camera.right();
    if (input.get(Command::MoveLeft))
        direction -= camera.right();
    if (input.get(Command::MoveUp))
        direction += camera.up();
    if (input.get(Command::MoveDown))
        direction -= camera.up();
    if (glm::dot(direction, direction) == 0.0f)
        return;

    const float speed = data.speed * (input.get(Command::Sprint) ? data.sprintMultiplier : 1.0f);
    data.position += glm::normalize(direction) * speed * std::clamp(deltaTime, 0.0f, 0.1f);
}
