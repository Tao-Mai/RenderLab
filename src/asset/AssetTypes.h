#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/SceneAsset.h"
#include "asset/TextureAsset.h"
#include "asset/pbrt/PbrtSceneAsset.h"
#include "asset/pbrt/PbrtTextureAsset.h"
#include "asset/pbrt/PbrtMediumAsset.h"

using AssetTypes =
    AssetTypeList<TextureAsset, MeshAsset, MaterialAsset, SceneAsset, EnvironmentMapAsset, SamplerAsset,
                  PbrtSceneAsset, PbrtTextureAsset, PbrtMediumAsset>;
