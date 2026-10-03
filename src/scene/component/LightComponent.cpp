#include "scene/component/LightComponent.h"

#include "scene/Actor.h"

void LightComponent::init(Actor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
}
