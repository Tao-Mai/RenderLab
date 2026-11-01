#include "asset/pbrt/PbrtMediumAsset.h"
#include "asset/pbrt/PbrtParameterReader.h"

#include <limits>

namespace pbrt_import
{
namespace
{
PbrtFloatArray array(const PbrtEntity& entity, std::string_view name, std::string_view type)
{
    const auto* parameter = findParameter(entity, name);
    if (!parameter) return PbrtFloatValues{};
    CHECK(parameter->type == type, "invalid PBRT medium array '{}' at {}:{}", name,
          parameter->location.file.string(), parameter->location.line);
    if (const auto* values = std::get_if<PbrtFloatValues>(&parameter->value)) return *values;
    const auto* reference = std::get_if<PbrtArrayReference>(&parameter->value);
    DCHECK(reference && reference->type == PbrtArrayType::Float64);
    return *reference;
}

uint64_t count(const PbrtFloatArray& values)
{
    return std::visit([]<class T>(const T& data) -> uint64_t
    {
        if constexpr (std::same_as<T, PbrtFloatValues>) return data.values.size();
        else return data.count;
    }, values);
}

glm::dvec3 point(const PbrtEntity& entity, std::string_view name, glm::dvec3 defaultValue)
{
    if (!findParameter(entity, name)) return defaultValue;
    const auto values = arrayParameter<PbrtFloatValues>(entity, name, "point3");
    CHECK(values.size() == 3, "invalid PBRT medium bound '{}' at {}:{}", name,
          entity.location.file.string(), entity.location.line);
    return {values[0], values[1], values[2]};
}

PbrtMediumAsset::Scattering scattering(ParameterReader& reader)
{
    PbrtMediumAsset::Scattering result;
    result.sigmaA = reader.spectrum("sigma_a", result.sigmaA);
    result.sigmaS = reader.spectrum("sigma_s", result.sigmaS);
    result.scale = reader.number("scale", result.scale);
    result.g = reader.number("g", result.g);
    return result;
}

PbrtMediumAsset::Temperature temperature(ParameterReader& reader, const PbrtEntity& entity)
{
    return {findParameter(entity, "temperatureoffset") ? reader.number("temperatureoffset", 0.0)
        : reader.number("temperaturecutoff", 0.0), reader.number("temperaturescale", 1.0)};
}

PbrtMediumAsset::Grid grid(ParameterReader& reader, const PbrtEntity& entity)
{
    PbrtMediumAsset::Grid result;
    result.dimensions = {reader.integer("nx", 1), reader.integer("ny", 1), reader.integer("nz", 1)};
    CHECK(result.dimensions.x > 0 && result.dimensions.y > 0 && result.dimensions.z > 0,
          "invalid PBRT grid dimensions at {}:{}", entity.location.file.string(), entity.location.line);
    result.pMin = point(entity, "p0", result.pMin);
    result.pMax = point(entity, "p1", result.pMax);
    return result;
}
}

PbrtMediumAsset convertMedium(PbrtMediumAsset::ID id, const std::string& name,
    const PbrtEntity& entity, const glm::dmat4& worldFromMedium)
{
    ParameterReader reader(entity);
    PbrtMediumAsset result;
    result.id = std::move(id);
    result.name = name;
    result.colorSpace = entity.colorSpace;
    result.location = entity.location;
    result.worldFromMedium = worldFromMedium;
    if (entity.type == "homogeneous")
    {
        PbrtMediumAsset::Homogeneous medium;
        medium.preset = reader.string("preset", "");
        medium.scattering = scattering(reader);
        medium.emission = {reader.spectrum("Le", PbrtConstantSpectrum{0.0}), reader.number("Lescale", 1.0)};
        result.model = std::move(medium);
    }
    else if (entity.type == "uniformgrid")
    {
        PbrtMediumAsset::UniformGrid medium;
        medium.grid = grid(reader, entity);
        medium.scattering = scattering(reader);
        medium.emission = {reader.spectrum("Le", PbrtConstantSpectrum{0.0}), 1.0};
        medium.density = array(entity, "density", "float");
        medium.temperature = array(entity, "temperature", "float");
        medium.emissionScale = array(entity, "Lescale", "float");
        medium.temperatureTransform = temperature(reader, entity);
        const auto dimensions = medium.grid.dimensions;
        const auto cells = uint64_t(dimensions.x) * dimensions.y * dimensions.z;
        CHECK(count(medium.density) == cells || count(medium.temperature) == cells,
              "PBRT uniform grid requires density or temperature samples at {}:{}", entity.location.file.string(), entity.location.line);
        for (const auto* values : {&medium.density, &medium.temperature, &medium.emissionScale})
            CHECK(count(*values) == 0 || count(*values) == cells, "invalid PBRT grid sample count at {}:{}",
                  entity.location.file.string(), entity.location.line);
        result.model = std::move(medium);
    }
    else if (entity.type == "rgbgrid")
    {
        PbrtMediumAsset::RgbGrid medium;
        medium.grid = grid(reader, entity);
        medium.sigmaA = array(entity, "sigma_a", "rgb");
        medium.sigmaS = array(entity, "sigma_s", "rgb");
        medium.radiance = array(entity, "Le", "rgb");
        medium.scale = reader.number("scale", medium.scale);
        medium.g = reader.number("g", medium.g);
        medium.emissionScale = reader.number("Lescale", medium.emissionScale);
        const auto d = medium.grid.dimensions;
        const auto values = uint64_t(d.x) * d.y * d.z * 3;
        for (const auto* samples : {&medium.sigmaA, &medium.sigmaS, &medium.radiance})
            CHECK(count(*samples) == 0 || count(*samples) == values, "invalid PBRT RGB grid sample count at {}:{}",
                  entity.location.file.string(), entity.location.line);
        result.model = std::move(medium);
    }
    else if (entity.type == "cloud")
    {
        PbrtMediumAsset::Cloud medium;
        medium.scattering = scattering(reader);
        medium.pMin = point(entity, "p0", medium.pMin);
        medium.pMax = point(entity, "p1", medium.pMax);
        medium.density = reader.number("density", medium.density);
        medium.wispiness = reader.number("wispiness", medium.wispiness);
        medium.frequency = reader.number("frequency", medium.frequency);
        result.model = std::move(medium);
    }
    else if (entity.type == "nanovdb")
    {
        PbrtMediumAsset::NanoVdb medium;
        const auto file = reader.file("filename");
        CHECK(file, "PBRT NanoVDB medium requires a file at {}:{}", entity.location.file.string(), entity.location.line);
        medium.file = *file;
        medium.densityGrid = reader.string("gridname", medium.densityGrid);
        medium.temperatureGrid = reader.string("temperaturename", medium.temperatureGrid);
        medium.scattering = scattering(reader);
        medium.temperatureTransform = temperature(reader, entity);
        medium.emissionScale = reader.number("Lescale", medium.emissionScale);
        result.model = std::move(medium);
    }
    else CHECK(false, "unsupported PBRT medium '{}' at {}:{}", entity.type, entity.location.file.string(), entity.location.line);
    return result;
}
}
