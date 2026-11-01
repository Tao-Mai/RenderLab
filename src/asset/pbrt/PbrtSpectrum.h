#pragma once

#include "asset/pbrt/PbrtTypes.h"

#include <glm/vec3.hpp>

struct PbrtFloatConstant
{
    double value = 0.0;
    bool operator==(const PbrtFloatConstant&) const = default;
};

using PbrtFloatInput = std::variant<PbrtFloatConstant, PbrtTextureReference>;

struct PbrtConstantSpectrum
{
    double value = 0.0;
    bool operator==(const PbrtConstantSpectrum&) const = default;
};

struct PbrtRgbSpectrum
{
    glm::dvec3 value{0.0};
    std::string colorSpace = "srgb";
    bool operator==(const PbrtRgbSpectrum&) const = default;
};

struct PbrtSampledSpectrum
{
    // Interleaved wavelength (nm), value pairs.
    std::variant<PbrtFloatValues, PbrtArrayReference> samples = PbrtFloatValues{};
    bool operator==(const PbrtSampledSpectrum&) const = default;
};

struct PbrtBlackbodySpectrum
{
    double temperature = 6500.0;
    bool operator==(const PbrtBlackbodySpectrum&) const = default;
};

struct PbrtNamedSpectrum
{
    std::string name;
    bool operator==(const PbrtNamedSpectrum&) const = default;
};

struct PbrtSpectrumFile
{
    PbrtFileReference file;
    bool operator==(const PbrtSpectrumFile&) const = default;
};

using PbrtSpectrum = std::variant<PbrtConstantSpectrum, PbrtRgbSpectrum, PbrtSampledSpectrum,
                                PbrtBlackbodySpectrum, PbrtNamedSpectrum, PbrtSpectrumFile>;
using PbrtSpectrumInput = std::variant<PbrtConstantSpectrum, PbrtRgbSpectrum, PbrtSampledSpectrum,
                                     PbrtBlackbodySpectrum, PbrtNamedSpectrum, PbrtSpectrumFile,
                                     PbrtTextureReference>;

namespace pbrt_import
{
[[nodiscard]] PbrtSpectrum readSpectrum(const PbrtParameter& parameter, bool albedo, bool allowFloat);
}
