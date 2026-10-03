#include "scene/actor/ALight.h"

#include "scene/component/LightComponent.h"

ALight::ALight()
{
    addComponent<LightComponent>();
}

void ALight::initialize()
{
    Actor::initialize();
    if (!getComponent<LightComponent>())
        throw std::logic_error("light actor requires LightComponent: " + name);
}

void ALight::tick(float deltaTime)
{
    tickComponents(deltaTime);
}
