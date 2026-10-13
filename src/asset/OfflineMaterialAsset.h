#pragma once

#include "asset/MaterialAsset.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <variant>

// RGB material descriptions; spectral data and shader evaluation are separate work.
struct OfflineMaterialAsset : MaterialAsset
{
    struct Diffuse
    {
        glm::vec3 reflectance{0.5f};
        bool      operator==(const Diffuse&) const = default;
    };

    struct Conductor
    {
        glm::vec3 eta{1.0f};
        glm::vec3 k{1.0f};
        glm::vec2 roughness{0.1f};
        bool      operator==(const Conductor&) const = default;
    };

    struct Dielectric
    {
        float     eta = 1.5f;
        glm::vec2 roughness{0.0f};
        bool      operator==(const Dielectric&) const = default;
    };

    std::variant<Diffuse, Conductor, Dielectric> parameters;
    bool                                         operator==(const OfflineMaterialAsset&) const = default;
};
