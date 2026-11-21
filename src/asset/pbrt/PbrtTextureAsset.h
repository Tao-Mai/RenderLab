#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/Asset.h"
#include "asset/TextureBinding.h"
#include "asset/pbrt/PbrtSpectrum.h"
#include <glm/vec2.hpp>

enum class RL_ENUM PbrtTextureType
{
    Float RL_ENUM_VALUE("Float"),
    Spectrum RL_ENUM_VALUE("Spectrum"),
};

struct RL_RECORD PbrtImageEncoding
{
    enum class RL_ENUM Kind
    {
        Linear RL_ENUM_VALUE("Linear"),
        Srgb RL_ENUM_VALUE("Srgb"),
        Gamma RL_ENUM_VALUE("Gamma"),
    };
    RL_FIELD("kind") Kind kind = Kind::Linear;
    RL_FIELD("gamma") double gamma = 1.0;
    bool operator==(const PbrtImageEncoding&) const = default;
};

using PbrtTextureInput = std::variant<PbrtFloatConstant, PbrtConstantSpectrum, PbrtRgbSpectrum,
    PbrtSampledSpectrum, PbrtBlackbodySpectrum, PbrtNamedSpectrum, PbrtSpectrumFile, PbrtTextureReference>;

struct RL_RECORD PbrtTextureMapping
{
    enum class RL_ENUM Kind
    {
        Uv RL_ENUM_VALUE("Uv"),
        Spherical RL_ENUM_VALUE("Spherical"),
        Cylindrical RL_ENUM_VALUE("Cylindrical"),
        Planar RL_ENUM_VALUE("Planar"),
        Spatial RL_ENUM_VALUE("Spatial"),
    };
    RL_FIELD("kind") Kind kind = Kind::Uv;
    RL_FIELD("scale") glm::dvec2 scale{1.0};
    RL_FIELD("offset") glm::dvec2 offset{0.0};
    RL_FIELD("uAxis") glm::dvec3 uAxis{1, 0, 0};
    RL_FIELD("vAxis") glm::dvec3 vAxis{0, 1, 0};
    RL_FIELD("textureFromWorld") glm::dmat4 textureFromWorld{1.0};
    bool operator==(const PbrtTextureMapping&) const = default;
};
struct RL_RECORD PbrtTextureAsset : Asset
{
    using ID                              = AssetID<PbrtTextureAsset>;
    static constexpr std::string_view dir = "PbrtTexture";

    struct RL_TYPE("Constant") Constant
    {
        RL_FIELD("value") PbrtTextureInput value = PbrtFloatConstant{1.0};
        bool operator==(const Constant&) const = default;
    };
    struct RL_TYPE("ImageMap") ImageMap
    {
        enum class RL_ENUM Filter
        {
            Point RL_ENUM_VALUE("Point"),
            Bilinear RL_ENUM_VALUE("Bilinear"),
            Trilinear RL_ENUM_VALUE("Trilinear"),
            Ewa RL_ENUM_VALUE("Ewa"),
        };
        enum class RL_ENUM Wrap
        {
            Repeat RL_ENUM_VALUE("Repeat"),
            Clamp RL_ENUM_VALUE("Clamp"),
            Black RL_ENUM_VALUE("Black"),
        };
        RL_FIELD("image") TextureBinding image;
        RL_FIELD("source") PbrtFileReference source;
        RL_FIELD("encoding") PbrtImageEncoding encoding;
        RL_FIELD("filter") Filter filter = Filter::Bilinear;
        RL_FIELD("wrap") Wrap wrap = Wrap::Repeat;
        RL_FIELD("maxAnisotropy") double maxAnisotropy = 8.0;
        RL_FIELD("scale") double scale = 1.0;
        RL_FIELD("invert") bool invert = false;
        bool operator==(const ImageMap&) const = default;
    };
    struct RL_TYPE("Scale") Scale
    {
        RL_FIELD("texture") PbrtTextureInput texture = PbrtFloatConstant{1.0};
        RL_FIELD("scale") PbrtFloatInput scale = PbrtFloatConstant{1.0};
        bool operator==(const Scale&) const = default;
    };
    struct RL_TYPE("Mix") Mix
    {
        RL_FIELD("first") PbrtTextureInput first = PbrtFloatConstant{0.0};
        RL_FIELD("second") PbrtTextureInput second = PbrtFloatConstant{1.0};
        RL_FIELD("amount") PbrtFloatInput amount = PbrtFloatConstant{0.5};
        bool operator==(const Mix&) const = default;
    };
    struct RL_TYPE("DirectionMix") DirectionMix
    {
        RL_FIELD("first") PbrtTextureInput first = PbrtFloatConstant{0.0};
        RL_FIELD("second") PbrtTextureInput second = PbrtFloatConstant{1.0};
        RL_FIELD("direction") glm::dvec3 direction{0, 1, 0};
        bool operator==(const DirectionMix&) const = default;
    };
    struct RL_TYPE("Bilerp") Bilerp
    {
        RL_FIELD("v00") PbrtTextureInput v00 = PbrtFloatConstant{0.0};
        RL_FIELD("v01") PbrtTextureInput v01 = PbrtFloatConstant{1.0};
        RL_FIELD("v10") PbrtTextureInput v10 = PbrtFloatConstant{0.0};
        RL_FIELD("v11") PbrtTextureInput v11 = PbrtFloatConstant{1.0};
        bool operator==(const Bilerp&) const = default;
    };
    struct RL_TYPE("Checkerboard") Checkerboard
    {
        RL_FIELD("first") PbrtTextureInput first = PbrtFloatConstant{1.0};
        RL_FIELD("second") PbrtTextureInput second = PbrtFloatConstant{0.0};
        RL_FIELD("dimension") int32_t dimension = 2;
        bool operator==(const Checkerboard&) const = default;
    };
    struct RL_TYPE("Dots") Dots
    {
        RL_FIELD("inside") PbrtTextureInput inside = PbrtFloatConstant{1.0};
        RL_FIELD("outside") PbrtTextureInput outside = PbrtFloatConstant{0.0};
        bool operator==(const Dots&) const = default;
    };
    struct RL_TYPE("Noise") Noise
    {
        enum class RL_ENUM Kind
        {
            Fbm RL_ENUM_VALUE("Fbm"),
            Wrinkled RL_ENUM_VALUE("Wrinkled"),
            Windy RL_ENUM_VALUE("Windy"),
        };
        RL_FIELD("kind") Kind kind = Kind::Fbm;
        RL_FIELD("octaves") int32_t octaves = 8;
        RL_FIELD("roughness") double roughness = 0.5;
        bool operator==(const Noise&) const = default;
    };
    struct RL_TYPE("Marble") Marble
    {
        RL_FIELD("octaves") int32_t octaves = 8;
        RL_FIELD("roughness") double roughness = 0.5;
        RL_FIELD("scale") double scale = 1.0;
        RL_FIELD("variation") double variation = 0.2;
        bool operator==(const Marble&) const = default;
    };
    struct RL_TYPE("Ptex") Ptex
    {
        RL_FIELD("file") PbrtFileReference file;
        RL_FIELD("encoding") PbrtImageEncoding encoding;
        RL_FIELD("scale") double scale = 1.0;
        bool operator==(const Ptex&) const = default;
    };
    using Model = std::variant<Constant, ImageMap, Scale, Mix, DirectionMix, Bilerp, Checkerboard, Dots, Noise, Marble, Ptex>;

    RL_FIELD("id") ID id;
    RL_FIELD("name") std::string name;
    RL_FIELD("valueType") PbrtTextureType valueType = PbrtTextureType::Float;
    RL_FIELD("location") PbrtSourceLocation location;
    RL_FIELD("mapping") PbrtTextureMapping mapping;
    RL_FIELD("model") Model model = Constant{};

    bool operator==(const PbrtTextureAsset&) const = default;
};

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtTextureAsset.generated.h")
