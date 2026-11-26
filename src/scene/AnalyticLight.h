#pragma once

#include "scene/Transform.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/constants.hpp>
#include <string>

enum class AnalyticLightType
{
    Point,
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

    float openingAngle = glm::pi<float>();  // Cone half-angle in radians; pi covers the full sphere.
    float penumbraAngle = 0.0f;             // Angular width of the transition inside the cone edge.

    glm::vec2 areaSize { 1.0f };

    bool operator==(const AnalyticLight&) const = default;
};
