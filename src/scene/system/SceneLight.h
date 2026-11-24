#pragma once

#include "asset/pbrt/PbrtLight.h"
#include <glm/mat4x4.hpp>
#include <vector>

struct SceneAnalyticLight
{
    struct Point
    {
        glm::vec3 position{};
        float     range                          = 0.0f;
        bool      operator==(const Point&) const = default;
    };
    struct Spot
    {
        glm::vec3 position{};
        glm::vec3 direction{0, 0, -1};
        float     cosInner        = 1.0f;
        float     cosOuter        = 1.0f;
        float     range           = 0.0f;
        float     falloffExponent = 2.0f;
        glm::vec3 right{1, 0, 0};
        bool      operator==(const Spot&) const = default;
    };
    struct Directional
    {
        glm::vec3 direction{0, 0, -1};
        bool      operator==(const Directional&) const = default;
    };
    struct Rectangle
    {
        glm::vec3 position{};
        glm::vec3 direction{0, 0, -1};
        glm::vec3 right{1, 0, 0};
        glm::vec2 size{};
        float     range                              = 0.0f;
        bool      operator==(const Rectangle&) const = default;
    };
    struct Projection
    {
        glm::vec3 position{};
        glm::mat4 lightFromWorld{1};
        float     fieldOfView                         = 90.0f;
        bool      operator==(const Projection&) const = default;
    };
    struct Goniometric
    {
        glm::vec3 position{};
        glm::mat4 lightFromWorld{1};
        bool      operator==(const Goniometric&) const = default;
    };

    using Model = std::variant<Point, Spot, Directional, Rectangle, Projection, Goniometric>;

    PbrtLightEmission emission;
    Model             model      = Point{};
    bool              castShadow = false;

    bool operator==(const SceneAnalyticLight&) const = default;
};

struct SceneEnvironmentLight
{
    glm::mat4              lightFromWorld{1};
    std::vector<glm::vec3> portal;
    PbrtLightEmission      emission;
    bool operator==(const SceneEnvironmentLight&) const = default;
};
