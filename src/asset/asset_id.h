#pragma once

#include <string>

using AssetId = std::string;

inline const AssetId kInvalidAssetId{};

namespace BuiltinId
{
inline const AssetId whiteTexture = "white";

inline const AssetId cubeMaterial = "cube";
inline const AssetId sphereMaterial = "sphere";
inline const AssetId arrowMaterial = "arrow";

inline const AssetId cubeMesh = "cube";
inline const AssetId sphereMesh = "sphere";
inline const AssetId arrowMesh = "arrow";

inline const AssetId sceneShader = "scene";
inline const AssetId lightShader = "light";
inline const AssetId pickingShader = "picking";

inline const AssetId defaultScene = "default";
}
