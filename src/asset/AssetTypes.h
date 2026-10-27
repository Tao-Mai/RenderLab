#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/SceneAsset.h"
#include "asset/ShaderAsset.h"
#include "asset/TextureAsset.h"
#include "asset/pbrt/PbrtSceneAsset.h"
#include "asset/pbrt/PbrtTextureAsset.h"

using AssetTypes =
    AssetTypeList<TextureAsset, MeshAsset, MaterialAsset, ShaderAsset, SceneAsset, EnvironmentMapAsset, SamplerAsset,
                  PbrtSceneAsset, PbrtTextureAsset>;
