#pragma once

#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct Light
{
    enum class Type
    {
        Point,
        Directional,
        RectArea,
        Spot,
    };

    std::string name{"Light"};
    Type type = Type::Point;

    glm::vec3 color{1.0f};
    glm::vec3 position{0.0f, 3.0f, 0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};

    float intensity = 1.0f;
    float range = 10.0f;
    float cosInner = 0.9396926f;
    float cosOuter = 0.8660254f;
    glm::vec2 areaSize{1.0f};

    bool enabled = true;
    bool castShadow = false;
};
