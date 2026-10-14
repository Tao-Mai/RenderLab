#include "core/math/Bounds.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

Aabb Aabb::transformed(const glm::mat4& model) const
{
    const glm::vec3 center      = glm::vec3{model * glm::vec4{(min + max) * 0.5f, 1.0f}};
    const glm::vec3 localExtent = (max - min) * 0.5f;
    const glm::vec3 extent      = glm::abs(glm::vec3{model[0]}) * localExtent.x +
        glm::abs(glm::vec3{model[1]}) * localExtent.y + glm::abs(glm::vec3{model[2]}) * localExtent.z;
    return {.min = center - extent, .max = center + extent};
}

Frustum::Frustum(const glm::mat4& viewProjection, NearPlane nearPlane)
{
    const auto rows = glm::transpose(viewProjection);
    planes = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1], rows[3] - rows[1], rows[3] - rows[2], rows[2]};
    planeCount = nearPlane == NearPlane::Include ? planes.size() : planes.size() - 1;
}

bool Frustum::intersects(const Aabb& bounds) const
{
    const glm::vec3 center          = (bounds.min + bounds.max) * 0.5f;
    const glm::vec3 extent          = (bounds.max - bounds.min) * 0.5f;
    constexpr float kPlaneTolerance = 1e-5f;
    for (size_t index = 0; index < planeCount; ++index)
    {
        const auto&     plane = planes[index];
        const glm::vec3 normal{plane};
        const float     distance = glm::dot(normal, center) + plane.w;
        const float     radius   = glm::dot(glm::abs(normal), extent);
        if (distance + radius < -kPlaneTolerance * glm::length(normal))
            return false;
    }
    return true;
}
