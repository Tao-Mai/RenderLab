#pragma once

#include "scene/Component.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct LightComponent : Component
{
    void init(Actor* actor) override;
    void tick(float) override {}

    enum class Type
    {
        Point,
        Directional,
        RectArea,
        Spot,
    };

    [[=ReflectField{}]] Type type = Type::Point;
    [[=ReflectField{}]] glm::vec3 color{1.0f};
    [[=ReflectField{}]] float intensity = 1.0f;
    [[=ReflectField{}]] float range = 10.0f;
    [[=ReflectField{}]] float cosInner = 0.9396926f;
    [[=ReflectField{}]] float cosOuter = 0.8660254f;
    [[=ReflectField{}]] glm::vec2 areaSize{1.0f};
    [[=ReflectField{}]] bool enabled = true;
    [[=ReflectField{}]] bool castShadow = false;
};
