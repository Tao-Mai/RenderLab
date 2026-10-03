#include "scene/actor/ALight.h"

#include "scene/component/LightComponent.h"

ALight::ALight()
{
    addDefaultComponent<LightComponent>();
}

void ALight::init()
{
    Actor::init();
    if (!getComponent<LightComponent>())
        throw std::logic_error("light actor requires LightComponent: " + name);
}
