#pragma once

#include "scene/component/LightComponent.h"

struct DirectionalLightComponent : LightComponent
{
    [[nodiscard]] glm::vec3 emissionColor() const override;

    [[=ReflectField{}]] float illuminance = 8.0f; // lux at perpendicular incidence.
};
