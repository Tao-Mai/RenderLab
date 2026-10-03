#include "scene/component/RenderComponent.h"

#include "scene/Actor.h"

void RenderComponent::init(Actor* actor)
{
    Component::init(actor);
    if (!actor->getComponent<TransformComponent>())
        throw std::logic_error("RenderComponent requires TransformComponent on actor: " + actor->name);
}
