#include "scene/actor/FreeFlyCameraActor.h"

#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/RenderComponent.h"

FreeFlyCameraActor::FreeFlyCameraActor()
{
    name = "Editor Camera";
    addDefaultComponent<CameraComponent>();
    addDefaultComponent<FreeFlyMoveComponent>();
}

void FreeFlyCameraActor::init()
{
    Actor::init();
    if (!getComponent<CameraComponent>() ||
        (!getComponent<FreeFlyMoveComponent>() && !getComponent<CharacterMoveComponent>()) ||
        (getComponent<FreeFlyMoveComponent>() && getComponent<CharacterMoveComponent>()) ||
        getComponent<RenderComponent>())
        throw std::logic_error("camera actor requires Camera and one movement type, without Render");

    camera().resetNavigation();
}

void FreeFlyCameraActor::tick(float deltaTime)
{
    // Update view direction before movement, independently of serialized component order.
    auto& view = camera();
    view.tick(deltaTime);
    for (const auto& component : components)
        if (component.get() != &view)
            component->tick(deltaTime);
}

CameraComponent& FreeFlyCameraActor::camera()
{
    return const_cast<CameraComponent&>(std::as_const(*this).camera());
}

const CameraComponent& FreeFlyCameraActor::camera() const
{
    auto* component = getComponent<CameraComponent>();
    if (!component) throw std::logic_error("camera actor requires CameraComponent");
    return *component;
}
