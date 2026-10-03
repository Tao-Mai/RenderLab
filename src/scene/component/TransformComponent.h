#pragma once

#include "scene/component/Component.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct TransformComponent : Component
{
    void tick(float) override {}

    [[=ReflectField{}]] glm::vec3 position{0.0f};
    [[=ReflectField{}]] glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    [[=ReflectField{}]] glm::vec3 scale{1.0f};

    [[nodiscard]] glm::vec3 rotationEulerDegrees() const;
    void setRotationEulerDegrees(const glm::vec3& eulerDegrees);
    [[nodiscard]] glm::mat4 matrix() const;
};
