#pragma once

namespace ecs
{
class FreeFlyMoveSystem
{
public:
    [[nodiscard]] bool tick(float deltaTime);
};
}
