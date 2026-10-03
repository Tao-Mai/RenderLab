#include "scene/component/LightComponent.h"

#include "scene/Actor.h"

void LightComponent::init(Actor* actor)
{
    Component::init(actor);
    if (!actor->getComponent<TransformComponent>())
        throw std::logic_error("LightComponent requires TransformComponent on actor: " + actor->name);
}
