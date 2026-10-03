#include "scene/component/RenderComponent.h"

#include "scene/AActor.h"

void RenderComponent::init(AActor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
}
