#pragma once

#include <glm/mat4x4.hpp>

struct SceneAsset;
struct PbrtSceneAsset;

namespace pbrt_import
{
class ImageImporter;
inline constexpr glm::dmat4 kEngineFromPbrtWorld{
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, -1, 0,
    0, 0, 0, 1,
};

void populateScene(SceneAsset& scene, const PbrtSceneAsset& source, ImageImporter& images);
}
