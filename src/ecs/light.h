#pragma once

#include "core/reflect.h"
#include "ecs/component.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ecs
{
struct Light
{
    enum class Type
    {
        Point,
        Directional,
        RectArea,
        Spot,
    };

    Type type = Type::Point;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float cosInner = 0.9396926f;
    float cosOuter = 0.8660254f;
    glm::vec2 areaSize{1.0f};
    bool enabled = true;
    bool castShadow = false;
};

static_assert(Component<Light>);

REFLECT_ENUM(Light::Type, LightType, Point, Directional, RectArea, Spot);

REFLECT(
    Light,
    type,
    color,
    intensity,
    range,
    cosInner,
    cosOuter,
    areaSize,
    enabled,
    castShadow);
}
