#include "scene/component/LightComponent.h"

#include "scene/AActor.h"

void LightComponent::init(AActor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
}
