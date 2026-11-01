#pragma once

#include "scene/component/LocalLightComponent.h"

struct ProjectionLightComponent : LocalLightComponent
{
    [[=ReflectField{}]] float fieldOfView = 90.0f;
};
