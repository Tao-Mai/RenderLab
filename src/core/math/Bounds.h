#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct Aabb
{
    glm::vec3 min{0.0f};

    glm::vec3 max{0.0f};

    bool               operator==(const Aabb&) const = default;
    [[nodiscard]] Aabb transformed(const glm::mat4& model) const;
};
