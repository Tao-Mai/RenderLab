#pragma once

#include "scene/Component.h"

struct CharacterMoveComponent : Component
{
    void init(Actor* actor) override;
    void tick(float deltaTime) override;

    [[=ReflectField{}]] float speed = 3.0f;
    [[=ReflectField{}]] float sprintMultiplier = 3.0f;
};
