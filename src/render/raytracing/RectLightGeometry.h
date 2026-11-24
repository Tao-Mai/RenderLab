#pragma once

#include "render/resources/ShaderData.h"

#include <array>
#include <glm/geometric.hpp>

inline constexpr std::array<glm::vec3, 4> kRectLightVertices{{
    {-0.5f, -0.5f, 0},
    {0.5f, -0.5f, 0},
    {0.5f, 0.5f, 0},
    {-0.5f, 0.5f, 0},
}};
inline constexpr std::array<uint32_t, 6>  kRectLightIndices{0, 2, 1, 0, 3, 2};

inline glm::mat4 rectLightTransform(const LightData& light)
{
    const glm::vec3 right = light.areaRight();
    const glm::vec3 direction{light.direction};
    const glm::vec3 up = glm::cross(right, direction);
    const glm::vec2 size{light.areaSizeCone};
    return {
        glm::vec4{right * size.x, 0},
        glm::vec4{up * size.y, 0},
        glm::vec4{-direction, 0},
        glm::vec4{light.position(), 1},
    };
}
