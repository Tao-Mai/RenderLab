#pragma once

#include "scene/component/LocalLightComponent.h"

struct SpotLightComponent : LocalLightComponent
{
    void init(AActor* actor) override;

    [[=ReflectField{}]] float cosInner = 0.9396926f;
    [[=ReflectField{}]] float cosOuter = 0.8660254f;
};
