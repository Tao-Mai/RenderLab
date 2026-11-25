#include "core/math/Bounds.h"

#include <glm/common.hpp>

Aabb Aabb::transformed(const glm::mat4& model) const
{
    const glm::vec3 center      = glm::vec3{model * glm::vec4{(min + max) * 0.5f, 1.0f}};
    const glm::vec3 localExtent = (max - min) * 0.5f;
    const glm::vec3 extent      = glm::abs(glm::vec3{model[0]}) * localExtent.x +
        glm::abs(glm::vec3{model[1]}) * localExtent.y + glm::abs(glm::vec3{model[2]}) * localExtent.z;
    return {.min = center - extent, .max = center + extent};
}
