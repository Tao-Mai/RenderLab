#include "scene/component/CameraComponent.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "scene/Actor.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

void CameraComponent::resetNavigation()
{
    looking = false;
    freeMovement = false;
    previousMouseX = 0.0;
    previousMouseY = 0.0;
    if (context().window != nullptr) context().window->setCursorCaptured(false);
}

void CameraComponent::setInputEnabled(bool enabled) { inputEnabled = enabled; }

void CameraComponent::tick(float deltaTime)
{
    CHECK(context().inputManager != nullptr && context().window != nullptr,
        "CameraComponent requires InputManager and Window");
    auto& window = *context().window;
    const auto& input = *context().inputManager;
    auto& camera = *this;

    if (inputEnabled && input.get(Command::ToggleFreeMovement))
        freeMovement = !freeMovement;

    const bool navigationRequested = freeMovement ||
        (input.get(Command::Navigate) && (inputEnabled || looking));
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

    if (!looking)
    {
        window.consumeScrollOffset();
        return;
    }

    const auto [x, y] = window.cursorPosition();
    camera.yaw += static_cast<float>(x - previousMouseX) * camera.mouseSensitivity;
    camera.pitch -= static_cast<float>(y - previousMouseY) * camera.mouseSensitivity;
    camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
    previousMouseX = x;
    previousMouseY = y;

    camera.fieldOfView = std::clamp(
        camera.fieldOfView - static_cast<float>(window.consumeScrollOffset()) * 2.0f,
        20.0f, 90.0f);
}

bool CameraComponent::isFreeMovementActive() const { return freeMovement; }
bool CameraComponent::isNavigationActive() const { return looking; }

const glm::vec3& CameraComponent::worldPosition() const
{
    return actor().transform().position;
}

glm::vec3 CameraComponent::forward() const
{
    const auto& camera = *this;
    const float yaw = glm::radians(camera.yaw);
    const float pitch = glm::radians(camera.pitch);
    return glm::normalize(glm::vec3{
        std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
}

glm::vec3 CameraComponent::right() const
{
    const float angle = glm::radians(yaw);
    return {-std::sin(angle), 0.0f, std::cos(angle)};
}

glm::vec3 CameraComponent::up() const
{
    return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 CameraComponent::viewMatrix() const
{
    return glm::lookAt(worldPosition(), worldPosition() + forward(), up());
}

glm::mat4 CameraComponent::projectionMatrix(float aspectRatio) const
{
    const auto& camera = *this;
    glm::mat4 projection = glm::perspective(
        glm::radians(camera.fieldOfView), aspectRatio, camera.nearPlane, camera.farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}
