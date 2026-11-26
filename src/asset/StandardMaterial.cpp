#include "asset/StandardMaterial.h"

#include "core/Logger.h"

#include <cmath>

void StandardMaterial::validate() const
{
    CHECK(shadingModel == ShadingModel::MetalRough || shadingModel == ShadingModel::SpecGloss);
    CHECK(alphaMode == AlphaMode::Opaque || alphaMode == AlphaMode::Mask);
    CHECK(std::isfinite(indexOfRefraction) && indexOfRefraction >= 1.0f, "invalid IOR in material '{}'", id);
    CHECK(std::isfinite(emissiveFactor) && emissiveFactor >= 0.0f, "invalid emission in material '{}'", id);
    CHECK(std::isfinite(normalScale) && normalScale >= 0.0f, "invalid normal scale in material '{}'", id);
    CHECK(std::isfinite(alphaCutoff) && alphaCutoff >= 0.0f && alphaCutoff <= 1.0f);
    CHECK(std::isfinite(diffuseTransmission) && diffuseTransmission >= 0.0f && diffuseTransmission <= 1.0f);
    CHECK(std::isfinite(specularTransmission) && specularTransmission >= 0.0f && specularTransmission <= 1.0f);
    for (glm::length_t channel = 0; channel < 4; ++channel)
    {
        CHECK(
            std::isfinite(baseColor[channel]) && baseColor[channel] >= 0.0f && baseColor[channel] <= 1.0f,
            "invalid base color in material '{}'",
            id
        );
        CHECK(
            std::isfinite(specularParams[channel]) && specularParams[channel] >= 0.0f &&
                specularParams[channel] <= 1.0f,
            "invalid specular parameters in material '{}'",
            id
        );
    }
    for (glm::length_t channel = 0; channel < 3; ++channel)
    {
        CHECK(
            std::isfinite(emissiveColor[channel]) && emissiveColor[channel] >= 0.0f,
            "invalid emissive color in material '{}'",
            id
        );
        CHECK(
            std::isfinite(transmissionColor[channel]) && transmissionColor[channel] >= 0.0f &&
                transmissionColor[channel] <= 1.0f,
            "invalid transmission color in material '{}'",
            id
        );
    }
}
