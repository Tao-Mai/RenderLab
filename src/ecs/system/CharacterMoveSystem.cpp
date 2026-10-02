#include "ecs/system/CharacterMoveSystem.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "ecs/SceneManager.h"
#include "ecs/component/CharacterMoveComponent.h"
#include "ecs/component/TransformComponent.h"
#include "ecs/system/CameraSystem.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace ecs
{
bool CharacterMoveSystem::tick(float deltaTime)
{
    CHECK(context().sceneManager != nullptr && context().cameraSystem != nullptr &&
        context().inputManager != nullptr,
        "CharacterMoveSystem requires SceneManager, CameraSystem and InputManager");
    if (!context().cameraSystem->isNavigationActive()) return false;

    auto& registry = context().sceneManager->registry();
    auto view = registry.view<TransformComponent, CharacterMoveComponent>();
    if (view.begin() == view.end()) return false;

    const auto& camera = *context().cameraSystem;
    const auto& input = *context().inputManager;
    glm::vec3 up = context().sceneManager->scene().environment.up;
    const float upLengthSquared = glm::dot(up, up);
    CHECK(std::isfinite(upLengthSquared) && upLengthSquared > 0.0f,
        "CharacterMove requires a finite nonzero Scene.Environment.Up");
    up /= std::sqrt(upLengthSquared);

    const glm::vec3 cameraFront = camera.forward();
    glm::vec3 front = cameraFront - up * glm::dot(cameraFront, up);
    constexpr float minimumProjectionLengthSquared = 1.0e-8f;
    if (glm::dot(front, front) > minimumProjectionLengthSquared)
    {
        front = glm::normalize(front);
    }
    else
    {
        // Looking along Up has no planar forward; preserve heading using the camera's right axis.
        const glm::vec3 cameraRight = camera.right();
        const glm::vec3 right = glm::normalize(cameraRight - up * glm::dot(cameraRight, up));
        front = glm::normalize(glm::cross(up, right));
    }
    const glm::vec3 right = glm::normalize(glm::cross(front, up));

    glm::vec3 direction{0.0f};
    if (input.get(Command::MoveForward)) direction += front;
    if (input.get(Command::MoveBackward)) direction -= front;
    if (input.get(Command::MoveRight)) direction += right;
    if (input.get(Command::MoveLeft)) direction -= right;
    if (glm::dot(direction, direction) == 0.0f) return false;

    direction = glm::normalize(direction);
    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    const bool sprint = input.get(Command::Sprint);

    bool changed = false;
    for (auto [entity, transform, move] : view.each())
    {
        const float speed = move.speed * (sprint ? move.sprintMultiplier : 1.0f);
        const glm::vec3 displacement = direction * speed * deltaTime;
        if (glm::dot(displacement, displacement) == 0.0f) continue;

        transform.position += displacement;
        changed = true;
    }
    return changed;
}
}
