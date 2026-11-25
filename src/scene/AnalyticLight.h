#pragma once

#include "scene/Transform.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>

enum class AnalyticLightType
{
    Point,
    Spot,
    Directional,
    Rectangle,
};

struct AnalyticLight
{
    std::string name;

    Transform transform;

    AnalyticLightType type = AnalyticLightType::Point;

    bool enabled = true;

    glm::vec3 color { 1.0f };
    float intensity = 8.0f;
    float range = 10.0f;
    
    glm::vec2 areaSize { 1.0f };
    float cosInner = 0.9396926f;
    float cosOuter = 0.8660254f;

    bool operator==(const AnalyticLight&) const = default;
};
