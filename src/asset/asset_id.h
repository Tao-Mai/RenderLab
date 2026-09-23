#pragma once

#include <cstdint>

using AssetId = std::uint32_t;

constexpr AssetId kInvalidAssetId = 0;
// [1, kFirstUserAssetId) reserved for builtins / engine assets.
constexpr AssetId kFirstUserAssetId = 10000;

namespace BuiltinId
{
// textures: 1-99
constexpr AssetId whiteTexture = 1;

// materials: 100-199
constexpr AssetId cubeMaterial = 100;
constexpr AssetId sphereMaterial = 101;
constexpr AssetId arrowMaterial = 102;

// meshes: 200-299
constexpr AssetId cubeMesh = 200;
constexpr AssetId sphereMesh = 201;
constexpr AssetId arrowMesh = 202;

// shaders: 300-399
constexpr AssetId sceneShader = 300;
constexpr AssetId lightShader = 301;
constexpr AssetId pickingShader = 302;

// scenes: 400-499
constexpr AssetId defaultScene = 400;
}
