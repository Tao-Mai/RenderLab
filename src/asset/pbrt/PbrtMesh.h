#pragma once

#include "asset/MeshAsset.h"
#include "asset/MeshGeometry.h"
#include "asset/pbrt/PbrtSceneAsset.h"

namespace pbrt_import
{
struct PreparedMesh
{
    MeshAsset asset;
    MeshGeometry geometry;
};

[[nodiscard]] std::vector<PreparedMesh> prepareMeshes(PbrtSceneAsset& scene, const std::filesystem::path& sourceRoot);
}  // namespace pbrt_import
