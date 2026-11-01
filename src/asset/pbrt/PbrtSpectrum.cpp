#include "asset/pbrt/PbrtSpectrum.h"
#include "asset/pbrt/PbrtFrontend.h"
#include "asset/pbrt/PbrtParameters.h"

#include <utility>

namespace pbrt_import
{
PbrtSpectrum readSpectrum(const PbrtParameter& parameter, bool albedo, bool allowFloat)
{
    const auto invalid = [&]
    {
        CHECK(false, "invalid PBRT spectrum '{}' at {}:{}", parameter.name,
              parameter.location.file.string(), parameter.location.line);
    };
    if (parameter.type == "spectrum")
    {
        if (const auto* values = std::get_if<PbrtStringValues>(&parameter.value))
        {
            CHECK(values->values.size() == 1, "expected one spectrum for '{}' at {}:{}", parameter.name,
                  parameter.location.file.string(), parameter.location.line);
            if (isNamedSpectrum(values->values.front()))
                return PbrtNamedSpectrum{values->values.front()};
            return PbrtSpectrumFile{parameterReference<PbrtFileReference>(parameter)};
        }
        if (const auto* values = std::get_if<PbrtFloatValues>(&parameter.value))
            return PbrtSampledSpectrum{*values};
        if (const auto* array = std::get_if<PbrtArrayReference>(&parameter.value))
        {
            DCHECK(array->type == PbrtArrayType::Float64 && array->count % 2 == 0);
            return PbrtSampledSpectrum{*array};
        }
    }
    const auto* values = std::get_if<PbrtFloatValues>(&parameter.value);
    if (!values)
    {
        invalid();
        std::unreachable();
    }
    if (parameter.type == "rgb" && values->values.size() == 3)
    {
        const glm::dvec3 rgb{values->values[0], values->values[1], values->values[2]};
        CHECK(rgb.x >= 0 && rgb.y >= 0 && rgb.z >= 0 &&
              (!albedo || (rgb.x <= 1 && rgb.y <= 1 && rgb.z <= 1)),
              "invalid PBRT RGB spectrum '{}' at {}:{}", parameter.name,
              parameter.location.file.string(), parameter.location.line);
        return PbrtRgbSpectrum{rgb, parameter.colorSpace};
    }
    if (parameter.type == "blackbody" && values->values.size() == 1)
        return PbrtBlackbodySpectrum{values->values[0]};
    if (allowFloat && parameter.type == "float" && values->values.size() == 1)
        return PbrtConstantSpectrum{values->values[0]};
    invalid();
    std::unreachable();
}

}  // namespace pbrt_import
