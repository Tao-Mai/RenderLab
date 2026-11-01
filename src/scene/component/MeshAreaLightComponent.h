#pragma once

#include "asset/pbrt/PbrtLight.h"
#include "scene/Component.h"

struct MeshAreaLightComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    [[=ReflectField{}]] uint32_t renderComponentIndex = 0;
    [[=ReflectField{}]] uint32_t submeshIndex = 0;
    [[=ReflectField{}]] PbrtLightEmission emission;
    [[=ReflectField{}]] bool twoSided = false;
    [[=ReflectField{}]] bool enabled = true;
};
