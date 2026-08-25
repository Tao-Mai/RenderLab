#pragma once

#include <cstdint>

#include <glm/glm.hpp>

enum class LightType : uint8_t
{
    Point = 0,
};

struct Light
{
    LightType type      = LightType::Point;
    glm::vec3 position{2.0f, 3.0f, 2.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    float     constant  = 1.0f;
    float     linear    = 0.09f;
    float     quadratic = 0.032f;

    static Light point(const glm::vec3& pos, const glm::vec3& color = {1.0f, 1.0f, 1.0f},
                       float intensity = 1.0f)
    {
        Light l;
        l.type      = LightType::Point;
        l.position  = pos;
        l.color     = color;
        l.intensity = intensity;
        return l;
    }
};

#include "base/core/reflection/meta_register.h"

META_REGISTER(Light, Light, position, color, intensity, constant, linear, quadratic);

                  
