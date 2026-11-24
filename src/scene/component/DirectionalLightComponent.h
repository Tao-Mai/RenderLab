#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LightComponent.h"

struct RL_REGISTER_TYPE(Component, "DirectionalLightComponent") DirectionalLightComponent : LightComponent
{
    [[nodiscard]] glm::vec3 emissionColor() const override;

    RL_FIELD("illuminance") float illuminance = 8.0f; // lux at perpendicular incidence.
};
