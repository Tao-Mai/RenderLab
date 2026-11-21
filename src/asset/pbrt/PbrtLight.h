#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/pbrt/PbrtSpectrum.h"
#include "asset/pbrt/PbrtImageReference.h"

// Photometric normalization is resolved when the offline spectrum is evaluated.
struct RL_RECORD PbrtLightEmission
{
    RL_FIELD("spectrum") PbrtSpectrum spectrum = PbrtRgbSpectrum{glm::dvec3{1.0}, "srgb"};
    RL_FIELD("scale") double scale = 1.0;
    RL_FIELD("power") std::optional<double> power;       // lm; point, spot, projection, goniometric and area lights.
    RL_FIELD("illuminance") std::optional<double> illuminance; // lx; distant and infinite lights.
    RL_FIELD("image") std::optional<PbrtImageReference> image;
    RL_FIELD("medium") std::optional<AssetID<PbrtMediumAsset>> medium;

    bool operator==(const PbrtLightEmission&) const = default;
};

namespace pbrt_import
{
class ImageImporter;
[[nodiscard]] PbrtLightEmission lightEmission(const PbrtEntity& entity, ImageImporter& images);
}

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtLight.generated.h")
