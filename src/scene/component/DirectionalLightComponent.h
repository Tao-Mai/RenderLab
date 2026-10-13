#pragma once

#include "scene/component/LightComponent.h"

struct DirectionalLightComponent : LightComponent
{
    static constexpr Type kType = Type::Directional;
    [[nodiscard]] Type type() const override { return kType; }
};
