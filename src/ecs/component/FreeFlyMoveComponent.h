#pragma once

#include "core/Reflect.h"
#include "ecs/component/Component.h"

namespace ecs
{
struct FreeFlyMoveComponent
{
    float speed = 3.0f;
    float sprintMultiplier = 3.0f;
};

static_assert(Component<FreeFlyMoveComponent>);

REFLECT(FreeFlyMoveComponent, FreeFlyMove, speed, sprintMultiplier);
}
