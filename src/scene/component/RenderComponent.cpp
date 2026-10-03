#include "scene/component/RenderComponent.h"

#include "scene/Actor.h"

void RenderComponent::init(Actor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
}
