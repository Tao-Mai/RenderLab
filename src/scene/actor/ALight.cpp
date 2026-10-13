#include "scene/actor/ALight.h"

#include "scene/component/PointLightComponent.h"

ALight::ALight() : ALight(std::make_unique<PointLightComponent>()) {}

ALight::ALight(std::unique_ptr<LightComponent> light)
{
    DCHECK(light);
    components.push_back(std::move(light));
}

void ALight::init()
{
    AActor::init();
    CHECK(getComponent<LightComponent>());
}
