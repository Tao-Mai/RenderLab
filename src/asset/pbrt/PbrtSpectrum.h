#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/pbrt/PbrtTypes.h"

#include <glm/vec3.hpp>

struct RL_TYPE("PbrtFloatConstant") PbrtFloatConstant
{
    RL_FIELD("value") double value = 0.0;
    bool operator==(const PbrtFloatConstant&) const = default;
};

using PbrtFloatInput = std::variant<PbrtFloatConstant, PbrtTextureReference>;

struct RL_TYPE("PbrtConstantSpectrum") PbrtConstantSpectrum
{
    RL_FIELD("value") double value = 0.0;
    bool operator==(const PbrtConstantSpectrum&) const = default;
};

struct RL_TYPE("PbrtRgbSpectrum") PbrtRgbSpectrum
{
    RL_FIELD("value") glm::dvec3 value{0.0};
    RL_FIELD("colorSpace") std::string colorSpace = "srgb";
    bool operator==(const PbrtRgbSpectrum&) const = default;
};

struct RL_TYPE("PbrtSampledSpectrum") PbrtSampledSpectrum
{
    // Interleaved wavelength (nm), value pairs.
    RL_FIELD("samples") std::variant<PbrtFloatValues, PbrtArrayReference> samples = PbrtFloatValues{};
    bool operator==(const PbrtSampledSpectrum&) const = default;
};

struct RL_TYPE("PbrtBlackbodySpectrum") PbrtBlackbodySpectrum
{
    RL_FIELD("temperature") double temperature = 6500.0;
    bool operator==(const PbrtBlackbodySpectrum&) const = default;
};

struct RL_TYPE("PbrtNamedSpectrum") PbrtNamedSpectrum
{
    RL_FIELD("name") std::string name;
    bool operator==(const PbrtNamedSpectrum&) const = default;
};

struct RL_TYPE("PbrtSpectrumFile") PbrtSpectrumFile
{
    RL_FIELD("file") PbrtFileReference file;
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

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtSpectrum.generated.h")
