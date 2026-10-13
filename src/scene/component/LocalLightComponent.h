#pragma once

#include "scene/component/LightComponent.h"

// Finite lights share a range; directional lights do not have one.
struct LocalLightComponent : LightComponent
{
    [[=ReflectField{}]] float range = 10.0f;
};
