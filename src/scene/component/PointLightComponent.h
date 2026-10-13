#pragma once

#include "scene/component/LocalLightComponent.h"

struct PointLightComponent : LocalLightComponent
{
    static constexpr Type kType = Type::Point;
    [[nodiscard]] Type type() const override { return kType; }
};
