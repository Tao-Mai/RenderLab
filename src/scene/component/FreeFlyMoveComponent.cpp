#include "scene/component/FreeFlyMoveComponent.h"

#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "scene/SceneManager.h"
#include "scene/component/TransformComponent.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/actor/FreeFlyCameraActor.h"

#include <algorithm>

#include <glm/geometric.hpp>

void FreeFlyMoveComponent::init(Actor* actor)
{
    Component::init(actor);
    if (!actor->getComponent<TransformComponent>())
        throw std::logic_error("FreeFlyMoveComponent requires TransformComponent on actor: " + actor->name);
    if (actor->getComponent<CharacterMoveComponent>())
        throw std::logic_error("FreeFlyMoveComponent cannot coexist with CharacterMoveComponent on actor: " + actor->name);
}

void FreeFlyMoveComponent::tick(float deltaTime)
{
    CHECK(context().sceneManager != nullptr && context().inputManager != nullptr,
        "movement requires SceneManager and InputManager");
    const auto& camera = context().sceneManager->editorCamera().camera();
    if (!camera.isNavigationActive()) return;

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
    if (glm::dot(direction, direction) == 0.0f) return;

    direction = glm::normalize(direction);
    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    const bool sprint = input.get(Command::Sprint);

    const float movementSpeed = speed * (sprint ? sprintMultiplier : 1.0f);
    const glm::vec3 displacement = direction * movementSpeed * deltaTime;
    if (glm::dot(displacement, displacement) == 0.0f) return;

    actor().transform().position += displacement;
}
