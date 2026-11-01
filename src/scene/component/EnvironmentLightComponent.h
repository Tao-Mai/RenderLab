#pragma once

#include "asset/pbrt/PbrtLight.h"
#include "scene/Component.h"

struct EnvironmentLightComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    [[=ReflectField{}]] PbrtLightEmission emission;
    // Portal vertices are in engine world coordinates, independent of map rotation.
    [[=ReflectField{}]] std::vector<glm::vec3> portal;
    [[=ReflectField{}]] bool enabled = true;
};
