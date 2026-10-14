#pragma once

#include <array>
#include <cstddef>
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

class Frustum
{
public:
    enum class NearPlane
    {
        Include,
        Exclude
    };

    // Vulkan clip volume: -w <= x,y <= w, 0 <= z <= w.
    explicit Frustum(const glm::mat4& viewProjection, NearPlane nearPlane = NearPlane::Include);
    [[nodiscard]] bool intersects(const Aabb& bounds) const;

private:
    std::array<glm::vec4, 6> planes;
    size_t                   planeCount;
};
