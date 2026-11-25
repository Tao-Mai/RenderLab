#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct Transform
{
    // World-space distances are in meters (1 unit = 1 m).
    glm::vec3 position { 0.0f };

    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };

    glm::vec3 scale { 1.0f };

    glm::mat4 base { 1.0f };

    bool operator==(const Transform&) const = default;

    [[nodiscard]] bool      setMatrix(const glm::mat4& matrix);
    [[nodiscard]] glm::mat4 trsMatrix() const;
    [[nodiscard]] glm::mat4 matrix() const;
};
