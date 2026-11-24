#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/MaterialAsset.h"
#include "asset/pbrt/PbrtSpectrum.h"
#include "asset/pbrt/PbrtImageReference.h"
#include "asset/pbrt/PbrtLight.h"

struct RL_REGISTER_TYPE(MaterialAsset, "PbrtMaterial") PbrtMaterial : MaterialAsset
{
    struct RL_RECORD Roughness
    {
        RL_FIELD("u") PbrtFloatInput u = PbrtFloatConstant{0.0};
        RL_FIELD("v") PbrtFloatInput v = PbrtFloatConstant{0.0};
        bool operator==(const Roughness&) const = default;
    };

    struct RL_TYPE("OpticalConstants") OpticalConstants
    {
        RL_FIELD("eta") PbrtSpectrumInput eta = PbrtNamedSpectrum{"metal-Cu-eta"};
        RL_FIELD("k") PbrtSpectrumInput k = PbrtNamedSpectrum{"metal-Cu-k"};
        bool operator==(const OpticalConstants&) const = default;
    };

    struct RL_TYPE("Reflectance") Reflectance
    {
        RL_FIELD("value") PbrtSpectrumInput value = PbrtConstantSpectrum{0.5};
        bool operator==(const Reflectance&) const = default;
    };
    using ConductorOptics = std::variant<OpticalConstants, Reflectance>;

    struct RL_RECORD Coating
    {
        RL_FIELD("roughness") Roughness roughness;
        RL_FIELD("eta") PbrtSpectrum eta = PbrtConstantSpectrum{1.5};
        RL_FIELD("thickness") PbrtFloatInput thickness = PbrtFloatConstant{0.01};
        RL_FIELD("g") PbrtFloatInput g = PbrtFloatConstant{0.0};
        RL_FIELD("albedo") PbrtSpectrumInput albedo = PbrtConstantSpectrum{0.0};
        RL_FIELD("maxDepth") int32_t maxDepth = 10;
        RL_FIELD("sampleCount") int32_t sampleCount = 1;
        bool operator==(const Coating&) const = default;
    };

    struct RL_TYPE("Diffuse") Diffuse
    {
        RL_FIELD("reflectance") PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.5};
        bool operator==(const Diffuse&) const = default;
    };

    struct RL_TYPE("Dielectric") Dielectric
    {
        RL_FIELD("eta") PbrtSpectrum eta = PbrtConstantSpectrum{1.5};
        RL_FIELD("roughness") Roughness roughness;
        RL_FIELD("remapRoughness") bool remapRoughness = true;
        bool operator==(const Dielectric&) const = default;
    };

    struct RL_TYPE("ThinDielectric") ThinDielectric
    {
        RL_FIELD("eta") PbrtSpectrum eta = PbrtConstantSpectrum{1.5};
        bool operator==(const ThinDielectric&) const = default;
    };

    struct RL_TYPE("Conductor") Conductor
    {
        RL_FIELD("optics") ConductorOptics optics = OpticalConstants{};
        RL_FIELD("roughness") Roughness roughness;
        RL_FIELD("remapRoughness") bool remapRoughness = true;
        bool operator==(const Conductor&) const = default;
    };

    struct RL_TYPE("CoatedDiffuse") CoatedDiffuse
    {
        RL_FIELD("reflectance") PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.5};
        RL_FIELD("coating") Coating coating;
        RL_FIELD("remapRoughness") bool remapRoughness = true;
        bool operator==(const CoatedDiffuse&) const = default;
    };

    struct RL_TYPE("CoatedConductor") CoatedConductor
    {
        RL_FIELD("optics") ConductorOptics optics = OpticalConstants{};
        RL_FIELD("conductorRoughness") Roughness conductorRoughness;
        RL_FIELD("coating") Coating coating;
        RL_FIELD("remapRoughness") bool remapRoughness = true;
        bool operator==(const CoatedConductor&) const = default;
    };

    struct RL_TYPE("DiffuseTransmission") DiffuseTransmission
    {
        RL_FIELD("reflectance") PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.25};
        RL_FIELD("transmittance") PbrtSpectrumInput transmittance = PbrtConstantSpectrum{0.25};
        RL_FIELD("scale") double scale = 1.0;
        bool operator==(const DiffuseTransmission&) const = default;
    };

    struct RL_TYPE("ScatteringCoefficients") ScatteringCoefficients
    {
        RL_FIELD("sigmaA") PbrtSpectrumInput sigmaA = PbrtRgbSpectrum{{0.0011f, 0.0024f, 0.014f}, "srgb"};
        RL_FIELD("sigmaS") PbrtSpectrumInput sigmaS = PbrtRgbSpectrum{{2.55f, 3.21f, 3.77f}, "srgb"};
        bool operator==(const ScatteringCoefficients&) const = default;
    };

    struct RL_TYPE("ScatteringReflectance") ScatteringReflectance
    {
        RL_FIELD("reflectance") PbrtSpectrumInput reflectance = PbrtConstantSpectrum{0.5};
        RL_FIELD("meanFreePath") PbrtSpectrumInput meanFreePath = PbrtConstantSpectrum{1.0};
        bool operator==(const ScatteringReflectance&) const = default;
    };

    struct RL_TYPE("NamedScattering") NamedScattering
    {
        RL_FIELD("name") std::string name;
        bool operator==(const NamedScattering&) const = default;
    };

    struct RL_TYPE("Subsurface") Subsurface
    {
        RL_FIELD("scattering") std::variant<ScatteringCoefficients, ScatteringReflectance, NamedScattering> scattering =
            ScatteringCoefficients{};
        RL_FIELD("scale") double scale = 1.0;
        RL_FIELD("eta") double eta = 1.33f;
        RL_FIELD("g") double g = 0.0;
        RL_FIELD("roughness") Roughness roughness;
        RL_FIELD("remapRoughness") bool remapRoughness = true;
        bool operator==(const Subsurface&) const = default;
    };

    struct RL_TYPE("HairAbsorption") HairAbsorption
    {
        RL_FIELD("sigmaA") PbrtSpectrumInput sigmaA = PbrtConstantSpectrum{0.0};
        bool operator==(const HairAbsorption&) const = default;
    };

    struct RL_TYPE("HairPigments") HairPigments
    {
        RL_FIELD("eumelanin") PbrtFloatInput eumelanin = PbrtFloatConstant{1.3};
        RL_FIELD("pheomelanin") PbrtFloatInput pheomelanin = PbrtFloatConstant{0.0};
        bool operator==(const HairPigments&) const = default;
    };

    struct RL_TYPE("Hair") Hair
    {
        RL_FIELD("absorption") std::variant<HairAbsorption, Reflectance, HairPigments> absorption = HairPigments{};
        RL_FIELD("eta") PbrtFloatInput eta = PbrtFloatConstant{1.55f};
        RL_FIELD("betaM") PbrtFloatInput betaM = PbrtFloatConstant{0.3f};
        RL_FIELD("betaN") PbrtFloatInput betaN = PbrtFloatConstant{0.3f};
        RL_FIELD("alpha") PbrtFloatInput alpha = PbrtFloatConstant{2.0};  // Degrees.
        bool operator==(const Hair&) const = default;
    };

    struct RL_TYPE("Measured") Measured
    {
        RL_FIELD("brdf") PbrtFileReference brdf;
        bool operator==(const Measured&) const = default;
    };

    struct RL_TYPE("Mix") Mix
    {
        RL_FIELD("first") MaterialAsset::ID first;
        RL_FIELD("second") MaterialAsset::ID second;
        RL_FIELD("amount") PbrtFloatInput amount = PbrtFloatConstant{0.5};
        bool operator==(const Mix&) const = default;
    };

    struct RL_TYPE("Interface") Interface
    {
        bool operator==(const Interface&) const = default;
    };

    using Model = std::variant<Diffuse, Dielectric, ThinDielectric, Conductor, CoatedDiffuse,
                               CoatedConductor, DiffuseTransmission, Subsurface, Hair, Measured, Mix, Interface>;

    struct RL_RECORD Emission
    {
        RL_FIELD("light") PbrtLightEmission light;
        RL_FIELD("twoSided") bool twoSided = false;
        bool operator==(const Emission&) const = default;
    };

    RL_FIELD("name") std::string name;
    RL_FIELD("colorSpace") std::string colorSpace = "srgb";
    RL_FIELD("location") PbrtSourceLocation location;
    RL_FIELD("model") Model model = Diffuse{};
    RL_FIELD("displacement") std::optional<PbrtFloatInput> displacement;
    RL_FIELD("normalMap") std::optional<PbrtImageReference> normalMap;
    RL_FIELD("emission") std::optional<Emission> emission;

    bool operator==(const PbrtMaterial&) const = default;
};

namespace pbrt_import
{
class ImageImporter;
[[nodiscard]] PbrtMaterial convertMaterial(MaterialAsset::ID id, const std::string& name,
    const PbrtEntity& entity, ImageImporter& images);
}
