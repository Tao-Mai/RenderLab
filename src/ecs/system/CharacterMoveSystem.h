#pragma once

namespace ecs
{
class CharacterMoveSystem
{
public:
    [[nodiscard]] bool tick(float deltaTime);
};
}
