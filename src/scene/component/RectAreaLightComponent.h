#pragma once

#include "scene/component/LocalLightComponent.h"

struct RectAreaLightComponent : LocalLightComponent
{
    [[nodiscard]] glm::vec3 emissionColor() const override;

    [[=ReflectField{}]] glm::vec2 areaSize{1.0f};
};
