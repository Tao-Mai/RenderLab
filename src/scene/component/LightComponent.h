#pragma once

#include "scene/Component.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct LightComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}
    [[nodiscard]] glm::vec4 photometricEmission() const;

    enum class Type : uint32_t
    {
        Point = 0,
        Directional = 1,
        RectArea = 2,
        Spot = 3,
    };

    [[=ReflectField{}]] Type type = Type::Point;
    [[=ReflectField{}]] glm::vec3 color{1.0f};
    // Point/RectArea: luminous flux (lm); Directional: illuminance (lux); Spot: luminous intensity (cd).
    [[=ReflectField{}]] float intensity = 1000.0f;
    [[=ReflectField{}]] float range = 10.0f;
    [[=ReflectField{}]] float cosInner = 0.9396926f;
    [[=ReflectField{}]] float cosOuter = 0.8660254f;
    [[=ReflectField{}]] glm::vec2 areaSize{1.0f};
    [[=ReflectField{}]] bool enabled = true;
    [[=ReflectField{}]] bool castShadow = false;
};
