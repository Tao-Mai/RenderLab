#include "scene/actor/ALight.h"

#include "scene/component/LightTypes.h"

ALight::ALight(LightComponent::Type type)
{
    components.push_back(createLightComponent(type));
}

void ALight::init()
{
    AActor::init();
    CHECK(getComponent<LightComponent>());
}
