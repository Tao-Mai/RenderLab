#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/Asset.h"
#include "asset/pbrt/PbrtSpectrum.h"

using PbrtFloatArray = std::variant<PbrtFloatValues, PbrtArrayReference>;

struct RL_RECORD PbrtMediumAsset : Asset
{
    using ID = AssetID<PbrtMediumAsset>;
    static constexpr std::string_view dir = "PbrtMedium";

    struct RL_RECORD Scattering
    {
        RL_FIELD("sigmaA") PbrtSpectrum sigmaA = PbrtConstantSpectrum{1.0};
        RL_FIELD("sigmaS") PbrtSpectrum sigmaS = PbrtConstantSpectrum{1.0};
        RL_FIELD("scale") double scale = 1.0;
        RL_FIELD("g") double g = 0.0;
        bool operator==(const Scattering&) const = default;
    };
    struct RL_RECORD Emission
    {
        RL_FIELD("radiance") PbrtSpectrum radiance = PbrtConstantSpectrum{0.0};
        RL_FIELD("scale") double scale = 1.0;
        bool operator==(const Emission&) const = default;
    };
    struct RL_TYPE("Homogeneous") Homogeneous
    {
        RL_FIELD("preset") std::string preset;
        RL_FIELD("scattering") Scattering scattering;
        RL_FIELD("emission") Emission emission;
        bool operator==(const Homogeneous&) const = default;
    };
    struct RL_RECORD Grid
    {
        RL_FIELD("dimensions") glm::ivec3 dimensions{1};
        RL_FIELD("pMin") glm::dvec3 pMin{0.0};
        RL_FIELD("pMax") glm::dvec3 pMax{1.0};
        bool operator==(const Grid&) const = default;
    };
    struct RL_RECORD Temperature
    {
        RL_FIELD("offset") double offset = 0.0;
        RL_FIELD("scale") double scale = 1.0;
        bool operator==(const Temperature&) const = default;
    };
    struct RL_TYPE("UniformGrid") UniformGrid
    {
        RL_FIELD("grid") Grid grid;
        RL_FIELD("scattering") Scattering scattering;
        RL_FIELD("emission") Emission emission;
        RL_FIELD("density") PbrtFloatArray density = PbrtFloatValues{};
        RL_FIELD("temperature") PbrtFloatArray temperature = PbrtFloatValues{};
        RL_FIELD("emissionScale") PbrtFloatArray emissionScale = PbrtFloatValues{};
        RL_FIELD("temperatureTransform") Temperature temperatureTransform;
        bool operator==(const UniformGrid&) const = default;
    };
    struct RL_TYPE("RgbGrid") RgbGrid
    {
        RL_FIELD("grid") Grid grid;
        RL_FIELD("sigmaA") PbrtFloatArray sigmaA = PbrtFloatValues{};
        RL_FIELD("sigmaS") PbrtFloatArray sigmaS = PbrtFloatValues{};
        RL_FIELD("radiance") PbrtFloatArray radiance = PbrtFloatValues{};
        RL_FIELD("scale") double scale = 1.0;
        RL_FIELD("g") double g = 0.0;
        RL_FIELD("emissionScale") double emissionScale = 1.0;
        bool operator==(const RgbGrid&) const = default;
    };
    struct RL_TYPE("Cloud") Cloud
    {
        RL_FIELD("scattering") Scattering scattering;
        RL_FIELD("pMin") glm::dvec3 pMin{0.0};
        RL_FIELD("pMax") glm::dvec3 pMax{1.0};
        RL_FIELD("density") double density = 1.0;
        RL_FIELD("wispiness") double wispiness = 1.0;
        RL_FIELD("frequency") double frequency = 5.0;
        bool operator==(const Cloud&) const = default;
    };
    struct RL_TYPE("NanoVdb") NanoVdb
    {
        RL_FIELD("file") PbrtFileReference file;
        RL_FIELD("densityGrid") std::string densityGrid = "density";
        RL_FIELD("temperatureGrid") std::string temperatureGrid = "temperature";
        RL_FIELD("scattering") Scattering scattering;
        RL_FIELD("temperatureTransform") Temperature temperatureTransform;
        RL_FIELD("emissionScale") double emissionScale = 1.0;
        bool operator==(const NanoVdb&) const = default;
    };
    using Model = std::variant<Homogeneous, UniformGrid, RgbGrid, Cloud, NanoVdb>;

    RL_FIELD("id") ID id;
    RL_FIELD("name") std::string name;
    RL_FIELD("colorSpace") std::string colorSpace;
    RL_FIELD("location") PbrtSourceLocation location;
    RL_FIELD("worldFromMedium") glm::dmat4 worldFromMedium{1.0};
    RL_FIELD("model") Model model = Homogeneous{};
    bool operator==(const PbrtMediumAsset&) const = default;
};

namespace pbrt_import
{
[[nodiscard]] PbrtMediumAsset convertMedium(PbrtMediumAsset::ID id, const std::string& name,
    const PbrtEntity& entity, const glm::dmat4& worldFromMedium);
}

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtMediumAsset.generated.h")
