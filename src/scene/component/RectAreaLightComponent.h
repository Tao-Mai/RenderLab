#pragma once

#include "scene/component/LocalLightComponent.h"

struct RectAreaLightComponent : LocalLightComponent
{
    static constexpr Type kType = Type::RectArea;
    [[nodiscard]] Type type() const override { return kType; }

    [[=ReflectField{}]] glm::vec2 areaSize{1.0f};

protected:
    [[nodiscard]] float emissionScale() const override;
};
