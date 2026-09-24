#pragma once

#include "core/Reflect.h"
#include "ecs/Component.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ecs
{
struct Transform
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    [[nodiscard]] glm::vec3 rotationEulerDegrees() const;
    void setRotationEulerDegrees(const glm::vec3& eulerDegrees);
    [[nodiscard]] glm::mat4 matrix() const;
};

static_assert(Component<Transform>);

REFLECT(Transform, position, rotation, scale);
}
