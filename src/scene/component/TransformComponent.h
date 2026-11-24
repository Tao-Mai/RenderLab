#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/Component.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct RL_REGISTER_TYPE(Component, "TransformComponent") TransformComponent : Component
{
    void tick(float) override {}

    // World-space distances are in meters (1 unit = 1 m).
    RL_FIELD("position") glm::vec3 position{0.0f};
    RL_FIELD("rotation") glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    RL_FIELD("scale") glm::vec3 scale{1.0f};
    RL_FIELD("base") glm::mat4 base{1.0f};

    [[nodiscard]] glm::vec3 rotationEulerDegrees() const;
    void setRotationEulerDegrees(const glm::vec3& eulerDegrees);
    [[nodiscard]] bool setMatrix(const glm::mat4& matrix);
    [[nodiscard]] glm::mat4 trsMatrix() const;
    [[nodiscard]] glm::mat4 matrix() const;
};
