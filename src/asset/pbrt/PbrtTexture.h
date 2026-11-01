#pragma once
#include "asset/pbrt/PbrtFrontend.h"
#include "asset/pbrt/PbrtImage.h"

namespace pbrt_import
{
[[nodiscard]] PbrtTextureAsset convertTexture(const ReadResult::TextureDescription& texture,
    const glm::dmat4& engineFromRender, ImageImporter& images);
}
