#include "scene/component/EnvironmentLightComponent.h"

#include "core/Logger.h"
#include "scene/AActor.h"
#include "scene/component/TransformComponent.h"

void EnvironmentLightComponent::init(AActor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
    CHECK(portal.empty() || portal.size() == 4, "environment portal requires four vertices on '{}'", actor->name);
}
