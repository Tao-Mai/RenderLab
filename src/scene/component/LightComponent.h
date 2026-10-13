#pragma once

#include "scene/Component.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct LightComponent : Component
{
    ~LightComponent() override = 0;
    void init(AActor* actor) override;
    void tick(float) override {}
    [[nodiscard]] glm::vec4 photometricEmission() const;

    [[=ReflectField{}]] glm::vec3 color{1.0f};
    // Point/Spot: luminous intensity (cd); Directional: illuminance (lux).
    // RectArea: whole-emitter luminous intensity along its normal (cd), independent of area.
    [[=ReflectField{}]] float intensity = 8.0f;
    [[=ReflectField{}]] bool enabled = true;
    [[=ReflectField{}]] bool castShadow = false;

protected:
    [[nodiscard]] virtual float emissionScale() const { return 1.0f; }
};
