#pragma once

#include "scene/component/Component.h"

struct FreeFlyMoveComponent : Component
{
    bool tick(float deltaTime) override;

    [[=ReflectField{}]] float speed = 3.0f;
    [[=ReflectField{}]] float sprintMultiplier = 3.0f;
};
