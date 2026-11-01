#include "asset/pbrt/PbrtTexture.h"
#include "asset/pbrt/PbrtParameterReader.h"

#include <glm/matrix.hpp>

namespace pbrt_import
{
namespace
{
glm::dvec3 vector(const PbrtEntity& entity, std::string_view name, glm::dvec3 defaultValue)
{
    if (!findParameter(entity, name)) return defaultValue;
    const auto values = arrayParameter<PbrtFloatValues>(entity, name, "vector3");
    CHECK(values.size() == 3, "invalid PBRT vector '{}' at {}:{}", name,
          entity.location.file.string(), entity.location.line);
    return {values[0], values[1], values[2]};
}

PbrtTextureInput input(ParameterReader& reader, PbrtTextureType type, std::string_view name, double defaultValue)
{
    if (type == PbrtTextureType::Float)
        return std::visit([](const auto& value) -> PbrtTextureInput { return value; }, reader.floatInput(name, defaultValue));
    return std::visit([](const auto& value) -> PbrtTextureInput { return value; },
        reader.spectrumInput(name, PbrtConstantSpectrum{defaultValue}, false));
}

PbrtTextureMapping mapping(const PbrtEntity& entity, const glm::dmat4& textureFromWorld, bool spatial)
{
    ParameterReader reader(entity);
    PbrtTextureMapping result;
    result.textureFromWorld = textureFromWorld;
    if (spatial)
    {
        result.kind = PbrtTextureMapping::Kind::Spatial;
        return result;
    }
    const auto name = reader.string("mapping", "uv");
    if (name == "uv") result.kind = PbrtTextureMapping::Kind::Uv;
    else if (name == "spherical") result.kind = PbrtTextureMapping::Kind::Spherical;
    else if (name == "cylindrical") result.kind = PbrtTextureMapping::Kind::Cylindrical;
    else if (name == "planar") result.kind = PbrtTextureMapping::Kind::Planar;
    else CHECK(false, "unknown PBRT texture mapping '{}' at {}:{}", name,
               entity.location.file.string(), entity.location.line);
    if (result.kind == PbrtTextureMapping::Kind::Uv)
        result.scale = {reader.number("uscale", 1.0), reader.number("vscale", 1.0)};
    if (result.kind == PbrtTextureMapping::Kind::Uv || result.kind == PbrtTextureMapping::Kind::Planar)
        result.offset = {reader.number("udelta", 0.0), reader.number("vdelta", 0.0)};
    if (result.kind == PbrtTextureMapping::Kind::Planar)
    {
        result.uAxis = vector(entity, "v1", result.uAxis);
        result.vAxis = vector(entity, "v2", result.vAxis);
    }
    return result;
}
}

PbrtTextureAsset convertTexture(const ReadResult::TextureDescription& source,
    const glm::dmat4& engineFromRender, ImageImporter& images)
{
    const auto& entity = source.entity;
    ParameterReader reader(entity);
    PbrtTextureAsset result;
    result.id = source.id;
    result.name = source.name;
    result.valueType = source.valueType;
    result.location = entity.location;
    const auto value = [&](std::string_view name, double defaultValue)
    {
        return input(reader, source.valueType, name, defaultValue);
    };
    bool spatial = false;
    if (entity.type == "constant") result.model = PbrtTextureAsset::Constant{value("value", 1.0)};
    else if (entity.type == "scale")
        result.model = PbrtTextureAsset::Scale{value("tex", 1.0), reader.floatInput("scale", 1.0)};
    else if (entity.type == "mix")
        result.model = PbrtTextureAsset::Mix{value("tex1", 0.0), value("tex2", 1.0), reader.floatInput("amount", 0.5)};
    else if (entity.type == "directionmix")
        result.model = PbrtTextureAsset::DirectionMix{value("tex1", 0.0), value("tex2", 1.0),
            glm::dvec3{engineFromRender * glm::dvec4{vector(entity, "dir", glm::dvec3{0, 1, 0}), 0}}};
    else if (entity.type == "bilerp")
        result.model = PbrtTextureAsset::Bilerp{value("v00", 0.0), value("v01", 1.0), value("v10", 0.0), value("v11", 1.0)};
    else if (entity.type == "checkerboard")
    {
        const auto dimension = reader.integer("dimension", 2);
        CHECK(dimension == 2 || dimension == 3, "invalid PBRT checkerboard dimension at {}:{}",
              entity.location.file.string(), entity.location.line);
        spatial = dimension == 3;
        result.model = PbrtTextureAsset::Checkerboard{value("tex1", 1.0), value("tex2", 0.0), dimension};
    }
    else if (entity.type == "dots")
        result.model = PbrtTextureAsset::Dots{value("inside", 1.0), value("outside", 0.0)};
    else if (entity.type == "fbm" || entity.type == "wrinkled" || entity.type == "windy")
    {
        CHECK(source.valueType == PbrtTextureType::Float, "PBRT noise requires float output at {}:{}",
              entity.location.file.string(), entity.location.line);
        PbrtTextureAsset::Noise noise;
        noise.kind = entity.type == "fbm" ? PbrtTextureAsset::Noise::Kind::Fbm
            : entity.type == "wrinkled" ? PbrtTextureAsset::Noise::Kind::Wrinkled : PbrtTextureAsset::Noise::Kind::Windy;
        if (noise.kind != PbrtTextureAsset::Noise::Kind::Windy)
        {
            noise.octaves = reader.integer("octaves", noise.octaves);
            noise.roughness = reader.number("roughness", noise.roughness);
        }
        CHECK(noise.octaves >= 0, "negative PBRT noise octaves at {}:{}", entity.location.file.string(), entity.location.line);
        result.model = noise;
        spatial = true;
    }
    else if (entity.type == "marble")
    {
        CHECK(source.valueType == PbrtTextureType::Spectrum, "PBRT marble requires spectrum output at {}:{}",
              entity.location.file.string(), entity.location.line);
        PbrtTextureAsset::Marble marble;
        marble.octaves = reader.integer("octaves", marble.octaves);
        marble.roughness = reader.number("roughness", marble.roughness);
        marble.scale = reader.number("scale", marble.scale);
        marble.variation = reader.number("variation", marble.variation);
        CHECK(marble.octaves >= 0, "negative PBRT marble octaves at {}:{}", entity.location.file.string(), entity.location.line);
        result.model = marble;
        spatial = true;
    }
    else if (entity.type == "ptex")
    {
        const auto file = reader.file("filename");
        CHECK(file, "PBRT Ptex requires a filename at {}:{}", entity.location.file.string(), entity.location.line);
        result.model = PbrtTextureAsset::Ptex{*file, imageEncoding(reader.string("encoding", "gamma 2.2"), entity.location),
            reader.number("scale", 1.0)};
    }
    else if (entity.type == "imagemap")
    {
        PbrtTextureAsset::ImageMap image;
        const auto file = reader.file("filename");
        CHECK(file, "PBRT imagemap requires a filename at {}:{}", entity.location.file.string(), entity.location.line);
        image.source = *file;
        image.encoding = findParameter(entity, "encoding") ? imageEncoding(reader.string("encoding", ""), entity.location)
            : defaultImageEncoding(*file);
        const auto filter = reader.string("filter", "bilinear");
        if (filter == "point") image.filter = PbrtTextureAsset::ImageMap::Filter::Point;
        else if (filter == "bilinear") image.filter = PbrtTextureAsset::ImageMap::Filter::Bilinear;
        else if (filter == "trilinear") image.filter = PbrtTextureAsset::ImageMap::Filter::Trilinear;
        else if (filter == "ewa") image.filter = PbrtTextureAsset::ImageMap::Filter::Ewa;
        else CHECK(false, "unknown PBRT image filter '{}' at {}:{}", filter, entity.location.file.string(), entity.location.line);
        const auto wrap = reader.string("wrap", "repeat");
        if (wrap == "repeat") image.wrap = PbrtTextureAsset::ImageMap::Wrap::Repeat;
        else if (wrap == "clamp") image.wrap = PbrtTextureAsset::ImageMap::Wrap::Clamp;
        else if (wrap == "black") image.wrap = PbrtTextureAsset::ImageMap::Wrap::Black;
        else CHECK(false, "unknown PBRT image wrap '{}' at {}:{}", wrap, entity.location.file.string(), entity.location.line);
        image.maxAnisotropy = reader.number("maxanisotropy", image.maxAnisotropy);
        image.scale = reader.number("scale", image.scale);
        image.invert = reader.boolean("invert", image.invert);
        image.image = images.texture(*file, image.encoding, image.filter, image.wrap, image.maxAnisotropy);
        result.model = std::move(image);
    }
    else
    {
        CHECK(false, "unsupported PBRT texture '{}' at {}:{}", entity.type, entity.location.file.string(), entity.location.line);
        std::unreachable();
    }
    const auto textureFromWorld = glm::inverse(engineFromRender * source.renderFromTexture.start);
    const bool mapped = spatial || entity.type == "imagemap" || entity.type == "bilerp" ||
        entity.type == "checkerboard" || entity.type == "dots";
    if (mapped) result.mapping = mapping(entity, textureFromWorld, spatial);
    else result.mapping.textureFromWorld = textureFromWorld;
    return result;
}
}
