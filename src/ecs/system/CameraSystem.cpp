#include "ecs/system/CameraSystem.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "ecs/SceneManager.h"
#include "ecs/component/TransformComponent.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ecs
{
void CameraSystem::reset()
{
    looking = false;
    freeMovement = false;
    previousMouseX = 0.0;
    previousMouseY = 0.0;
    if (context().window != nullptr) context().window->setCursorCaptured(false);
}

bool CameraSystem::tick(bool allowModeToggle)
{
    CHECK(context().sceneManager != nullptr && context().inputManager != nullptr &&
        context().window != nullptr, "CameraSystem requires SceneManager, InputManager and Window");
    auto& window = *context().window;
    const auto& input = *context().inputManager;
    auto& camera = context().sceneManager->registry().get<CameraComponent>(
        context().sceneManager->editorCamera());

    if (allowModeToggle && input.get(Command::ToggleFreeMovement))
        freeMovement = !freeMovement;

    const bool navigationRequested = freeMovement ||
        (input.get(Command::Navigate) && (allowModeToggle || looking));
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
        return false;
    }

    const float previousYaw = camera.yaw;
    const float previousPitch = camera.pitch;
    const float previousFieldOfView = camera.fieldOfView;
    const auto [x, y] = window.cursorPosition();
    camera.yaw += static_cast<float>(x - previousMouseX) * camera.mouseSensitivity;
    camera.pitch -= static_cast<float>(y - previousMouseY) * camera.mouseSensitivity;
    camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
    previousMouseX = x;
    previousMouseY = y;

    camera.fieldOfView = std::clamp(
        camera.fieldOfView - static_cast<float>(window.consumeScrollOffset()) * 2.0f,
        20.0f, 90.0f);

    return camera.yaw != previousYaw || camera.pitch != previousPitch ||
        camera.fieldOfView != previousFieldOfView;
}

bool CameraSystem::isFreeMovementActive() const { return freeMovement; }
bool CameraSystem::isNavigationActive() const { return looking; }

const CameraComponent& CameraSystem::component() const
{
    CHECK(context().sceneManager != nullptr, "CameraSystem requires SceneManager");
    return context().sceneManager->registry().get<CameraComponent>(context().sceneManager->editorCamera());
}

const glm::vec3& CameraSystem::worldPosition() const
{
    return context().sceneManager->registry().get<TransformComponent>(
        context().sceneManager->editorCamera()).position;
}

glm::vec3 CameraSystem::forward() const
{
    const auto& camera = component();
    const float yaw = glm::radians(camera.yaw);
    const float pitch = glm::radians(camera.pitch);
    return glm::normalize(glm::vec3{
        std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
}

glm::vec3 CameraSystem::right() const
{
    const float yaw = glm::radians(component().yaw);
    return {-std::sin(yaw), 0.0f, std::cos(yaw)};
}

glm::vec3 CameraSystem::up() const
{
    return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 CameraSystem::viewMatrix() const
{
    return glm::lookAt(worldPosition(), worldPosition() + forward(), up());
}

glm::mat4 CameraSystem::projectionMatrix(float aspectRatio) const
{
    const auto& camera = component();
    glm::mat4 projection = glm::perspective(
        glm::radians(camera.fieldOfView), aspectRatio, camera.nearPlane, camera.farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}
}
