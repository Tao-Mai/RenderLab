#include "scene/actor/AFreeFlyCamera.h"

#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/RenderComponent.h"

AFreeFlyCamera::AFreeFlyCamera()
{
    name = "Editor Camera";
    addDefaultComponent<CameraComponent>();
    addDefaultComponent<FreeFlyMoveComponent>();
}

void AFreeFlyCamera::init()
{
    AActor::init();
    CHECK(getComponent<CameraComponent>());
    CHECK(getComponent<FreeFlyMoveComponent>() || getComponent<CharacterMoveComponent>(),
          "camera actor requires one movement type");
    CHECK(!getComponent<RenderComponent>());

    camera().resetNavigation();
}

void AFreeFlyCamera::tick(float deltaTime)
{
    // Update view direction before movement, independently of serialized component order.
    auto& view = camera();
    view.tick(deltaTime);
    for (const auto& component : components)
        if (component.get() != &view)
            component->tick(deltaTime);
}

CameraComponent& AFreeFlyCamera::camera()
{
    return const_cast<CameraComponent&>(std::as_const(*this).camera());
}

const CameraComponent& AFreeFlyCamera::camera() const
{
    auto* component = getComponent<CameraComponent>();
    DCHECK(component);
    return *component;
}
