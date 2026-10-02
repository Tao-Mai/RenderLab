#include "scene/actor/FreeFlyCameraActor.h"

#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/RenderComponent.h"

FreeFlyCameraActor::FreeFlyCameraActor()
{
    name = "Editor Camera";
    addComponent<CameraComponent>();
    addComponent<FreeFlyMoveComponent>();
}

void FreeFlyCameraActor::initialize()
{
    Actor::initialize();
    if (!getComponent<CameraComponent>() ||
        (!getComponent<FreeFlyMoveComponent>() && !getComponent<CharacterMoveComponent>()) ||
        (getComponent<FreeFlyMoveComponent>() && getComponent<CharacterMoveComponent>()) ||
        getComponent<RenderComponent>())
        throw std::logic_error("camera actor requires Camera and one movement component, without Render");

    camera().resetNavigation();
}

bool FreeFlyCameraActor::tick(float deltaTime)
{
    // Update view direction before movement, independently of serialized component order.
    auto& view = camera();
    bool changed = view.tick(deltaTime);
    for (const auto& component : components)
        if (component.get() != &view)
            changed = component->tick(deltaTime) || changed;

    return changed;
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
