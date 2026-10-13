#pragma once

#include "scene/component/LocalLightComponent.h"

struct RectAreaLightComponent : LocalLightComponent
{
    [[=ReflectField{}]] glm::vec2 areaSize{1.0f};

protected:
    [[nodiscard]] float emissionScale() const override;
};
