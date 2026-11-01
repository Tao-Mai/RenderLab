#pragma once

#include "asset/MaterialAsset.h"
#include "asset/pbrt/PbrtSpectrum.h"
#include "asset/pbrt/PbrtImageReference.h"

struct PbrtMaterial : MaterialAsset
{
    struct Roughness
    {
        PbrtFloatInput u = PbrtFloatConstant{0.0};
        PbrtFloatInput v = PbrtFloatConstant{0.0};
        bool operator==(const Roughness&) const = default;
    };

    struct OpticalConstants
    {
        PbrtSpectrumInput eta = PbrtNamedSpectrum{"metal-Cu-eta"};
        PbrtSpectrumInput k = PbrtNamedSpectrum{"metal-Cu-k"};
        bool operator==(const OpticalConstants&) const = default;
    };

    struct Reflectance
    {
        PbrtSpectrumInput value = PbrtConstantSpectrum{0.5};
        bool operator==(const Reflectance&) const = default;
    };
    using ConductorOptics = std::variant<OpticalConstants, Reflectance>;

    struct Coating
    {
        Roughness roughness;
        PbrtSpectrum eta = PbrtConstantSpectrum{1.5};
        PbrtFloatInput thickness = PbrtFloatConstant{0.01};
        PbrtFloatInput g = PbrtFloatConstant{0.0};
        PbrtSpectrumInput albedo = PbrtConstantSpectrum{0.0};
        int32_t maxDepth = 10;
        int32_t sampleCount = 1;
        bool operator==(const Coating&) const = default;
    };

    struct Diffuse
    {
        PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.5};
        bool operator==(const Diffuse&) const = default;
    };

    struct Dielectric
    {
        PbrtSpectrum eta = PbrtConstantSpectrum{1.5};
        Roughness roughness;
        bool remapRoughness = true;
        bool operator==(const Dielectric&) const = default;
    };

    struct ThinDielectric
    {
        PbrtSpectrum eta = PbrtConstantSpectrum{1.5};
        bool operator==(const ThinDielectric&) const = default;
    };

    struct Conductor
    {
        ConductorOptics optics = OpticalConstants{};
        Roughness roughness;
        bool remapRoughness = true;
        bool operator==(const Conductor&) const = default;
    };

    struct CoatedDiffuse
    {
        PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.5};
        Coating coating;
        bool remapRoughness = true;
        bool operator==(const CoatedDiffuse&) const = default;
    };

    struct CoatedConductor
    {
        ConductorOptics optics = OpticalConstants{};
        Roughness conductorRoughness;
        Coating coating;
        bool remapRoughness = true;
        bool operator==(const CoatedConductor&) const = default;
    };

    struct DiffuseTransmission
    {
        PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.25};
        PbrtSpectrumInput transmittance = PbrtConstantSpectrum{0.25};
        double scale = 1.0;
        bool operator==(const DiffuseTransmission&) const = default;
    };

    struct ScatteringCoefficients
    {
        PbrtSpectrumInput sigmaA = PbrtRgbSpectrum{{0.0011f, 0.0024f, 0.014f}, "srgb"};
        PbrtSpectrumInput sigmaS = PbrtRgbSpectrum{{2.55f, 3.21f, 3.77f}, "srgb"};
        bool operator==(const ScatteringCoefficients&) const = default;
    };

    struct ScatteringReflectance
    {
        PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.5};
        PbrtSpectrumInput meanFreePath = PbrtConstantSpectrum{1.0};
        bool operator==(const ScatteringReflectance&) const = default;
    };

    struct NamedScattering
    {
        std::string name;
        bool operator==(const NamedScattering&) const = default;
    };

    struct Subsurface
    {
        std::variant<ScatteringCoefficients, ScatteringReflectance, NamedScattering> scattering =
            ScatteringCoefficients{};
        double scale = 1.0;
        double eta = 1.33f;
        double g = 0.0;
        Roughness roughness;
        bool remapRoughness = true;
        bool operator==(const Subsurface&) const = default;
    };

    struct HairAbsorption
    {
        PbrtSpectrumInput sigmaA = PbrtConstantSpectrum{0.0};
        bool operator==(const HairAbsorption&) const = default;
    };

    struct HairPigments
    {
        PbrtFloatInput eumelanin = PbrtFloatConstant{1.3};
        PbrtFloatInput pheomelanin = PbrtFloatConstant{0.0};
        bool operator==(const HairPigments&) const = default;
    };

    struct Hair
    {
        std::variant<HairAbsorption, Reflectance, HairPigments> absorption = HairPigments{};
        PbrtFloatInput eta = PbrtFloatConstant{1.55f};
        PbrtFloatInput betaM = PbrtFloatConstant{0.3f};
        PbrtFloatInput betaN = PbrtFloatConstant{0.3f};
        PbrtFloatInput alpha = PbrtFloatConstant{2.0};  // Degrees.
        bool operator==(const Hair&) const = default;
    };

    struct Measured
    {
        PbrtFileReference brdf;
        bool operator==(const Measured&) const = default;
    };

    struct Mix
    {
        MaterialAsset::ID first;
        MaterialAsset::ID second;
        PbrtFloatInput amount = PbrtFloatConstant{0.5};
        bool operator==(const Mix&) const = default;
    };

    struct Interface
    {
        bool operator==(const Interface&) const = default;
    };

    using Model = std::variant<Diffuse, Dielectric, ThinDielectric, Conductor, CoatedDiffuse,
                               CoatedConductor, DiffuseTransmission, Subsurface, Hair, Measured, Mix, Interface>;

    std::string name;
    std::string colorSpace = "srgb";
    PbrtSourceLocation location;
    Model model = Diffuse{};
    std::optional<PbrtFloatInput> displacement;
    std::optional<PbrtImageReference> normalMap;

    bool operator==(const PbrtMaterial&) const = default;
};

namespace pbrt_import
{
class ImageImporter;
[[nodiscard]] PbrtMaterial convertMaterial(MaterialAsset::ID id, const std::string& name,
    const PbrtEntity& entity, ImageImporter& images);
}
