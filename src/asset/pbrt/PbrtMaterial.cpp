#include "asset/pbrt/PbrtMaterial.h"

#include "asset/pbrt/PbrtParameterReader.h"
#include "asset/pbrt/PbrtImage.h"

#include <algorithm>
#include <utility>

namespace pbrt_import
{
namespace
{
class MaterialReader : public ParameterReader
{
public:
    using ParameterReader::ParameterReader;

    PbrtMaterial::Roughness roughness(const std::string& prefix = {})
    {
        const auto common = floatInput(prefix + "roughness", 0.0);
        return {optionalFloat(prefix + "uroughness").value_or(common),
                optionalFloat(prefix + "vroughness").value_or(common)};
    }

    PbrtMaterial::ConductorOptics conductorOptics(const std::string& prefix = {})
    {
        const auto reflectance = optionalSpectrum("reflectance", true);
        const auto eta = optionalSpectrum(prefix + "eta", false);
        const auto k = optionalSpectrum(prefix + "k", false);
        CHECK(!reflectance || (!eta && !k), "PBRT conductor combines reflectance with eta/k at {}:{}",
              entity.location.file.string(), entity.location.line);
        if (reflectance)
            return PbrtMaterial::Reflectance{*reflectance};
        return PbrtMaterial::OpticalConstants{eta.value_or(PbrtNamedSpectrum{"metal-Cu-eta"}),
                                             k.value_or(PbrtNamedSpectrum{"metal-Cu-k"})};
    }

    PbrtMaterial::Coating coating(const std::string& prefix = {})
    {
        PbrtMaterial::Coating result;
        result.roughness = roughness(prefix);
        result.eta = spectrum(prefix + "eta", result.eta);
        result.thickness = floatInput("thickness", std::get<PbrtFloatConstant>(result.thickness).value);
        result.g = floatInput("g", std::get<PbrtFloatConstant>(result.g).value);
        result.albedo = spectrumInput("albedo", result.albedo, true);
        result.maxDepth = integer("maxdepth", result.maxDepth);
        result.sampleCount = integer("nsamples", result.sampleCount);
        return result;
    }

    PbrtMaterial::Mix mix()
    {
        const auto* value = parameter("materials");
        CHECK(value, "PBRT mix requires two materials at {}:{}", entity.location.file.string(), entity.location.line);
        const auto names = arrayParameter<PbrtStringValues>(entity, "materials", "string");
        CHECK(names.size() == 2, "PBRT mix requires two materials at {}:{}",
              value->location.file.string(), value->location.line);
        return {parameterReference<PbrtMaterialReference>(*value, 0).id,
                parameterReference<PbrtMaterialReference>(*value, 1).id, floatInput("amount", 0.5)};
    }

};

PbrtMaterial::Model materialModel(const PbrtEntity& entity, MaterialReader& reader)
{
    if (entity.type == "diffuse")
    {
        PbrtMaterial::Diffuse result;
        result.reflectance = reader.spectrumInput("reflectance", result.reflectance, true);
        return result;
    }
    if (entity.type == "dielectric")
    {
        PbrtMaterial::Dielectric result;
        result.eta = reader.spectrum("eta", result.eta);
        result.roughness = reader.roughness();
        result.remapRoughness = reader.boolean("remaproughness", result.remapRoughness);
        return result;
    }
    if (entity.type == "thindielectric")
    {
        PbrtMaterial::ThinDielectric result;
        result.eta = reader.spectrum("eta", result.eta);
        return result;
    }
    if (entity.type == "conductor")
    {
        PbrtMaterial::Conductor result;
        result.optics = reader.conductorOptics();
        result.roughness = reader.roughness();
        result.remapRoughness = reader.boolean("remaproughness", result.remapRoughness);
        return result;
    }
    if (entity.type == "coateddiffuse")
    {
        PbrtMaterial::CoatedDiffuse result;
        result.reflectance = reader.spectrumInput("reflectance", result.reflectance, true);
        result.coating = reader.coating();
        result.remapRoughness = reader.boolean("remaproughness", result.remapRoughness);
        return result;
    }
    if (entity.type == "coatedconductor")
    {
        PbrtMaterial::CoatedConductor result;
        result.optics = reader.conductorOptics("conductor.");
        result.conductorRoughness = reader.roughness("conductor.");
        result.coating = reader.coating("interface.");
        result.remapRoughness = reader.boolean("remaproughness", result.remapRoughness);
        return result;
    }
    if (entity.type == "diffusetransmission")
    {
        PbrtMaterial::DiffuseTransmission result;
        result.reflectance = reader.spectrumInput("reflectance", result.reflectance, true);
        result.transmittance = reader.spectrumInput("transmittance", result.transmittance, true);
        result.scale = reader.number("scale", result.scale);
        return result;
    }
    if (entity.type == "subsurface")
    {
        PbrtMaterial::Subsurface result;
        result.scale = reader.number("scale", result.scale);
        result.eta = reader.number("eta", result.eta);
        result.g = reader.number("g", result.g);
        result.roughness = reader.roughness();
        result.remapRoughness = reader.boolean("remaproughness", result.remapRoughness);
        const auto name = reader.string("name", "");
        if (!name.empty())
        {
            result.scattering = PbrtMaterial::NamedScattering{name};
            result.g = 0.0;
        }
        else
        {
            const auto sigmaA = reader.optionalSpectrum("sigma_a", false);
            const auto sigmaS = reader.optionalSpectrum("sigma_s", false);
            CHECK(bool(sigmaA) == bool(sigmaS), "PBRT subsurface requires both sigma_a and sigma_s at {}:{}",
                  entity.location.file.string(), entity.location.line);
            if (sigmaA)
                result.scattering = PbrtMaterial::ScatteringCoefficients{*sigmaA, *sigmaS};
            else if (const auto reflectance = reader.optionalSpectrum("reflectance", true))
                result.scattering = PbrtMaterial::ScatteringReflectance{
                    *reflectance, reader.spectrumInput("mfp", PbrtConstantSpectrum{1.0}, false)};
        }
        return result;
    }
    if (entity.type == "hair")
    {
        PbrtMaterial::Hair result;
        result.eta = reader.floatInput("eta", std::get<PbrtFloatConstant>(result.eta).value);
        result.betaM = reader.floatInput("beta_m", std::get<PbrtFloatConstant>(result.betaM).value);
        result.betaN = reader.floatInput("beta_n", std::get<PbrtFloatConstant>(result.betaN).value);
        result.alpha = reader.floatInput("alpha", std::get<PbrtFloatConstant>(result.alpha).value);
        // Absorption input precedence follows the pinned HairMaterial::Create.
        const auto sigmaA = reader.optionalSpectrum("sigma_a", false);
        const auto reflectance = reader.optionalSpectrum("reflectance", true);
        const auto color = reader.optionalSpectrum("color", true);
        const auto eumelanin = reader.optionalFloat("eumelanin");
        const auto pheomelanin = reader.optionalFloat("pheomelanin");
        if (sigmaA)
            result.absorption = PbrtMaterial::HairAbsorption{*sigmaA};
        else if (reflectance || color)
            result.absorption = PbrtMaterial::Reflectance{reflectance ? *reflectance : *color};
        else
        {
            PbrtMaterial::HairPigments pigments;
            pigments.eumelanin = eumelanin.value_or(pigments.eumelanin);
            pigments.pheomelanin = pheomelanin.value_or(pigments.pheomelanin);
            result.absorption = std::move(pigments);
        }
        return result;
    }
    if (entity.type == "measured")
    {
        const auto brdf = reader.file("filename");
        CHECK(brdf, "PBRT measured material requires a file at {}:{}",
              entity.location.file.string(), entity.location.line);
        return PbrtMaterial::Measured{*brdf};
    }
    if (entity.type == "mix")
        return reader.mix();
    if (entity.type == "interface")
        return PbrtMaterial::Interface{};
    CHECK(false, "unknown PBRT material '{}' at {}:{}", entity.type,
          entity.location.file.string(), entity.location.line);
    std::unreachable();
}
}  // namespace

PbrtMaterial convertMaterial(MaterialAsset::ID id, const std::string& name, const PbrtEntity& entity, ImageImporter& images)
{
    MaterialReader reader(entity);
    PbrtMaterial result;
    result.id = std::move(id);
    result.name = name;
    result.colorSpace = entity.colorSpace;
    result.location = entity.location;
    result.model = materialModel(entity, reader);
    result.displacement = reader.optionalFloat("displacement");
    if (const auto file = reader.file("normalmap"))
        result.normalMap = PbrtImageReference{*file, images.texture(*file, {})};
    return result;
}
}  // namespace pbrt_import
