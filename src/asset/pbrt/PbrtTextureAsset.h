#pragma once

#include "asset/Asset.h"
#include "asset/TextureBinding.h"
#include "asset/pbrt/PbrtSpectrum.h"
#include <glm/vec2.hpp>

enum class PbrtTextureType { Float, Spectrum };

struct PbrtImageEncoding
{
    enum class Kind { Linear, Srgb, Gamma };
    Kind kind = Kind::Linear;
    double gamma = 1.0;
    bool operator==(const PbrtImageEncoding&) const = default;
};

using PbrtTextureInput = std::variant<PbrtFloatConstant, PbrtConstantSpectrum, PbrtRgbSpectrum,
    PbrtSampledSpectrum, PbrtBlackbodySpectrum, PbrtNamedSpectrum, PbrtSpectrumFile, PbrtTextureReference>;

struct PbrtTextureMapping
{
    enum class Kind { Uv, Spherical, Cylindrical, Planar, Spatial };
    Kind kind = Kind::Uv;
    glm::dvec2 scale{1.0};
    glm::dvec2 offset{0.0};
    glm::dvec3 uAxis{1, 0, 0};
    glm::dvec3 vAxis{0, 1, 0};
    glm::dmat4 textureFromWorld{1.0};
    bool operator==(const PbrtTextureMapping&) const = default;
};
struct PbrtTextureAsset : Asset
{
    using ID                              = AssetID<PbrtTextureAsset>;
    static constexpr std::string_view dir = "PbrtTexture";

    struct Constant
    {
        PbrtTextureInput value = PbrtFloatConstant{1.0};
        bool operator==(const Constant&) const = default;
    };
    struct ImageMap
    {
        enum class Filter { Point, Bilinear, Trilinear, Ewa };
        enum class Wrap { Repeat, Clamp, Black };
        TextureBinding image;
        PbrtFileReference source;
        PbrtImageEncoding encoding;
        Filter filter = Filter::Bilinear;
        Wrap wrap = Wrap::Repeat;
        double maxAnisotropy = 8.0;
        double scale = 1.0;
        bool invert = false;
        bool operator==(const ImageMap&) const = default;
    };
    struct Scale
    {
        PbrtTextureInput texture = PbrtFloatConstant{1.0};
        PbrtFloatInput scale = PbrtFloatConstant{1.0};
        bool operator==(const Scale&) const = default;
    };
    struct Mix
    {
        PbrtTextureInput first = PbrtFloatConstant{0.0};
        PbrtTextureInput second = PbrtFloatConstant{1.0};
        PbrtFloatInput amount = PbrtFloatConstant{0.5};
        bool operator==(const Mix&) const = default;
    };
    struct DirectionMix
    {
        PbrtTextureInput first = PbrtFloatConstant{0.0};
        PbrtTextureInput second = PbrtFloatConstant{1.0};
        glm::dvec3 direction{0, 1, 0};
        bool operator==(const DirectionMix&) const = default;
    };
    struct Bilerp
    {
        PbrtTextureInput v00 = PbrtFloatConstant{0.0};
        PbrtTextureInput v01 = PbrtFloatConstant{1.0};
        PbrtTextureInput v10 = PbrtFloatConstant{0.0};
        PbrtTextureInput v11 = PbrtFloatConstant{1.0};
        bool operator==(const Bilerp&) const = default;
    };
    struct Checkerboard
    {
        PbrtTextureInput first = PbrtFloatConstant{1.0};
        PbrtTextureInput second = PbrtFloatConstant{0.0};
        int32_t dimension = 2;
        bool operator==(const Checkerboard&) const = default;
    };
    struct Dots
    {
        PbrtTextureInput inside = PbrtFloatConstant{1.0};
        PbrtTextureInput outside = PbrtFloatConstant{0.0};
        bool operator==(const Dots&) const = default;
    };
    struct Noise
    {
        enum class Kind { Fbm, Wrinkled, Windy };
        Kind kind = Kind::Fbm;
        int32_t octaves = 8;
        double roughness = 0.5;
        bool operator==(const Noise&) const = default;
    };
    struct Marble
    {
        int32_t octaves = 8;
        double roughness = 0.5;
        double scale = 1.0;
        double variation = 0.2;
        bool operator==(const Marble&) const = default;
    };
    struct Ptex
    {
        PbrtFileReference file;
        PbrtImageEncoding encoding;
        double scale = 1.0;
        bool operator==(const Ptex&) const = default;
    };
    using Model = std::variant<Constant, ImageMap, Scale, Mix, DirectionMix, Bilerp, Checkerboard, Dots, Noise, Marble, Ptex>;

    ID id;
    std::string name;
    PbrtTextureType valueType = PbrtTextureType::Float;
    PbrtSourceLocation location;
    PbrtTextureMapping mapping;
    Model model = Constant{};

    bool operator==(const PbrtTextureAsset&) const = default;
};
