#pragma once

#include "asset/pbrt/PbrtSpectrum.h"
#include "asset/pbrt/PbrtImageReference.h"

// Photometric normalization is resolved when the offline spectrum is evaluated.
struct PbrtLightEmission
{
    PbrtSpectrum spectrum = PbrtRgbSpectrum{glm::dvec3{1.0}, "srgb"};
    double scale = 1.0;
    std::optional<double> power;       // lm; point, spot, projection, goniometric and area lights.
    std::optional<double> illuminance; // lx; distant and infinite lights.
    std::optional<PbrtImageReference> image;
    std::optional<AssetID<PbrtMediumAsset>> medium;

    bool operator==(const PbrtLightEmission&) const = default;
};

namespace pbrt_import
{
class ImageImporter;
[[nodiscard]] PbrtLightEmission lightEmission(const PbrtEntity& entity, ImageImporter& images);
}
