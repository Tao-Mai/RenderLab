#pragma once

#include "asset/Asset.h"
#include "asset/pbrt/PbrtSpectrum.h"

using PbrtFloatArray = std::variant<PbrtFloatValues, PbrtArrayReference>;

struct PbrtMediumAsset : Asset
{
    using ID = AssetID<PbrtMediumAsset>;
    static constexpr std::string_view dir = "PbrtMedium";

    struct Scattering
    {
        PbrtSpectrum sigmaA = PbrtConstantSpectrum{1.0};
        PbrtSpectrum sigmaS = PbrtConstantSpectrum{1.0};
        double scale = 1.0;
        double g = 0.0;
        bool operator==(const Scattering&) const = default;
    };
    struct Emission
    {
        PbrtSpectrum radiance = PbrtConstantSpectrum{0.0};
        double scale = 1.0;
        bool operator==(const Emission&) const = default;
    };
    struct Homogeneous
    {
        std::string preset;
        Scattering scattering;
        Emission emission;
        bool operator==(const Homogeneous&) const = default;
    };
    struct Grid
    {
        glm::ivec3 dimensions{1};
        glm::dvec3 pMin{0.0}, pMax{1.0};
        bool operator==(const Grid&) const = default;
    };
    struct Temperature
    {
        double offset = 0.0;
        double scale = 1.0;
        bool operator==(const Temperature&) const = default;
    };
    struct UniformGrid
    {
        Grid grid;
        Scattering scattering;
        Emission emission;
        PbrtFloatArray density = PbrtFloatValues{};
        PbrtFloatArray temperature = PbrtFloatValues{};
        PbrtFloatArray emissionScale = PbrtFloatValues{};
        Temperature temperatureTransform;
        bool operator==(const UniformGrid&) const = default;
    };
    struct RgbGrid
    {
        Grid grid;
        PbrtFloatArray sigmaA = PbrtFloatValues{};
        PbrtFloatArray sigmaS = PbrtFloatValues{};
        PbrtFloatArray radiance = PbrtFloatValues{};
        double scale = 1.0, g = 0.0, emissionScale = 1.0;
        bool operator==(const RgbGrid&) const = default;
    };
    struct Cloud
    {
        Scattering scattering;
        glm::dvec3 pMin{0.0}, pMax{1.0};
        double density = 1.0, wispiness = 1.0, frequency = 5.0;
        bool operator==(const Cloud&) const = default;
    };
    struct NanoVdb
    {
        PbrtFileReference file;
        std::string densityGrid = "density", temperatureGrid = "temperature";
        Scattering scattering;
        Temperature temperatureTransform;
        double emissionScale = 1.0;
        bool operator==(const NanoVdb&) const = default;
    };
    using Model = std::variant<Homogeneous, UniformGrid, RgbGrid, Cloud, NanoVdb>;

    ID id;
    std::string name;
    std::string colorSpace;
    PbrtSourceLocation location;
    glm::dmat4 worldFromMedium{1.0};
    Model model = Homogeneous{};
    bool operator==(const PbrtMediumAsset&) const = default;
};

namespace pbrt_import
{
[[nodiscard]] PbrtMediumAsset convertMedium(PbrtMediumAsset::ID id, const std::string& name,
    const PbrtEntity& entity, const glm::dmat4& worldFromMedium);
}
