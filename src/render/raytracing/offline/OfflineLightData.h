#pragma once

#include "asset/pbrt/PbrtLight.h"

// Native scene descriptions. The offline GPU preparation resolves spectra,
// image resources and sampling distributions from these records.
struct OfflineLightData
{
    struct Point
    {
        bool operator==(const Point&) const = default;
    };
    struct Spot
    {
        float cosInner;
        float cosOuter;
        bool operator==(const Spot&) const = default;
    };
    struct Directional
    {
        bool operator==(const Directional&) const = default;
    };
    struct MeshArea
    {
        uint32_t drawItemIndex;
        bool twoSided;
        bool reverseOrientation;
        bool operator==(const MeshArea&) const = default;
    };
    struct Environment
    {
        std::vector<glm::vec3> portal;
        bool operator==(const Environment&) const = default;
    };
    struct Projection
    {
        float fieldOfView;
        bool operator==(const Projection&) const = default;
    };
    struct Goniometric
    {
        bool operator==(const Goniometric&) const = default;
    };

    using Model = std::variant<Point, Spot, Directional, MeshArea, Environment, Projection, Goniometric>;

    // Analytic forward is -Z; environment/goniometric maps retain PBRT local coordinates.
    glm::mat4 worldFromLight{1.0f};
    PbrtLightEmission emission;
    Model model = Point{};

    bool operator==(const OfflineLightData&) const = default;
};
