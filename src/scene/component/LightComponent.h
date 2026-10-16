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
    [[nodiscard]] virtual glm::vec3 emissionColor() const = 0;

    [[=ReflectField{}]] glm::vec3 color{1.0f};
    [[=ReflectField{}]] bool enabled = true;
    [[=ReflectField{}]] bool castShadow = false;

protected:
    [[nodiscard]] glm::vec3 coloredEmission(float strength) const;
};
