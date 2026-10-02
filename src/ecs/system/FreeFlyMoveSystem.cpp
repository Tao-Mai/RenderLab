#include "ecs/system/FreeFlyMoveSystem.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "ecs/SceneManager.h"
#include "ecs/component/FreeFlyMoveComponent.h"
#include "ecs/component/TransformComponent.h"
#include "ecs/system/CameraSystem.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace ecs
{
bool FreeFlyMoveSystem::tick(float deltaTime)
{
    CHECK(context().sceneManager != nullptr && context().cameraSystem != nullptr &&
        context().inputManager != nullptr,
        "FreeFlyMoveSystem requires SceneManager, CameraSystem and InputManager");
    if (!context().cameraSystem->isNavigationActive()) return false;

    auto& registry = context().sceneManager->registry();
    auto view = registry.view<TransformComponent, FreeFlyMoveComponent>();
    if (view.begin() == view.end()) return false;

    const auto& camera = *context().cameraSystem;
    const auto& input = *context().inputManager;
    const glm::vec3 front = camera.forward();
    const glm::vec3 right = camera.right();
    const glm::vec3 up = camera.up();

    glm::vec3 direction{0.0f};
    if (input.get(Command::MoveForward)) direction += front;
    if (input.get(Command::MoveBackward)) direction -= front;
    if (input.get(Command::MoveRight)) direction += right;
    if (input.get(Command::MoveLeft)) direction -= right;
    if (input.get(Command::MoveUp)) direction += up;
    if (input.get(Command::MoveDown)) direction -= up;
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
