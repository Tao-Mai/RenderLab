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
    CHECK(getComponent<CameraComponent>());
    CHECK(getComponent<FreeFlyMoveComponent>() || getComponent<CharacterMoveComponent>(),
          "camera actor requires one movement type");
    CHECK(!getComponent<RenderComponent>());

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
    CHECK(component);
    return *component;
}
