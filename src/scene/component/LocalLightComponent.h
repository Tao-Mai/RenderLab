#pragma once

#include "scene/component/LightComponent.h"

// Finite lights share luminous intensity and a range.
struct LocalLightComponent : LightComponent
{
    [[nodiscard]] glm::vec3 emissionColor() const override;

    // RectArea uses the whole emitter's normal-direction intensity, independent of area.
    [[=ReflectField{}]] float luminousIntensity = 8.0f; // cd.
    [[=ReflectField{}]] float range = 10.0f;
};
