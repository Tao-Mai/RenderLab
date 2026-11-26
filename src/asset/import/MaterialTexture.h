#pragma once

#include "asset/StandardMaterial.h"

namespace asset_import
{
// Bake source-format multipliers into Falcor's texture-or-uniform slots.
[[nodiscard]] std::optional<TextureBinding>
bakeMaterialTexture(const std::optional<TextureBinding>& texture, glm::vec4 factor);

[[nodiscard]] std::optional<TextureBinding> packMetalRoughTextures(
    const std::optional<TextureBinding>& metallic,
    const std::optional<TextureBinding>& roughness,
    float                                metallicFactor,
    float                                roughnessFactor
);

// glTF's colored dielectric specular extension is represented by StandardMaterial's SG model.
void convertToSpecGloss(
    StandardMaterial&                    material,
    float                                specularFactor,
    glm::vec3                            specularColor,
    const std::optional<TextureBinding>& specularTexture,
    const std::optional<TextureBinding>& specularColorTexture
);
}  // namespace asset_import
