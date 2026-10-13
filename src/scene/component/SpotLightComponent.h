#pragma once

#include "scene/component/LocalLightComponent.h"

struct SpotLightComponent : LocalLightComponent
{
    static constexpr Type kType = Type::Spot;
    [[nodiscard]] Type type() const override { return kType; }

    [[=ReflectField{}]] float cosInner = 0.9396926f;
    [[=ReflectField{}]] float cosOuter = 0.8660254f;
};
