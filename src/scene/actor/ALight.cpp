#include "scene/actor/ALight.h"

#include "scene/component/LightComponent.h"

ALight::ALight()
{
    addDefaultComponent<LightComponent>();
}

void ALight::init()
{
    Actor::init();
    CHECK(getComponent<LightComponent>());
}
