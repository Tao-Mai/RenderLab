#include "asset/pbrt/PbrtLight.h"
#include "asset/pbrt/PbrtParameters.h"
#include "asset/pbrt/PbrtImage.h"

#include <cmath>

namespace pbrt_import
{
namespace
{
constexpr double kUnspecifiedNormalization = -1.0;
}

PbrtLightEmission lightEmission(const PbrtEntity& entity, ImageImporter& images)
{
    PbrtLightEmission result;
    const bool intensity = entity.type == "point" || entity.type == "spot" || entity.type == "goniometric";
    const auto* spectrum = entity.type == "projection" ? nullptr : findParameter(entity, intensity ? "I" : "L");
    result.spectrum = spectrum ? readSpectrum(*spectrum, false, false)
                               : PbrtSpectrum{PbrtRgbSpectrum{glm::dvec3{1.0}, entity.colorSpace}};
    result.scale = scalarParameter<PbrtFloatValues>(entity, "scale", "float", result.scale);
    CHECK(std::isfinite(result.scale) && result.scale >= 0.0, "invalid PBRT light scale at {}:{}",
          entity.location.file.string(), entity.location.line);

    const bool illuminance = entity.type == "distant" || entity.type == "infinite";
    const auto name = illuminance ? "illuminance" : "power";
    const double normalization = scalarParameter<PbrtFloatValues>(entity, name, "float", kUnspecifiedNormalization);
    CHECK(std::isfinite(normalization), "invalid PBRT light {} at {}:{}", name,
          entity.location.file.string(), entity.location.line);
    if (normalization > 0.0)
    {
        if (illuminance)
            result.illuminance = normalization;
        else
            result.power = normalization;
    }

    const bool imageLight = entity.type == "diffuse" || entity.type == "infinite" ||
        entity.type == "projection" || entity.type == "goniometric";
    if (imageLight && findParameter(entity, "filename"))
    {
        const auto filenames = arrayParameter<PbrtStringValues>(entity, "filename", "string");
        CHECK(filenames.size() == 1, "expected one PBRT light image at {}:{}",
              entity.location.file.string(), entity.location.line);
        if (!filenames.front().empty())
        {
            const auto file = parameterReference<PbrtFileReference>(*findParameter(entity, "filename"));
            result.image = PbrtImageReference{file, images.texture(file, defaultImageEncoding(file))};
        }
    }
    if (entity.type == "diffuse" || entity.type == "infinite")
        CHECK(!spectrum || !result.image, "PBRT light combines L and filename at {}:{}",
              entity.location.file.string(), entity.location.line);
    if (entity.type == "projection")
        CHECK(result.image, "PBRT projection light requires an image at {}:{}",
              entity.location.file.string(), entity.location.line);
    return result;
}
}  // namespace pbrt_import
