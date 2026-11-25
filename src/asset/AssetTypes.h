#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/GltfMaterial.h"
#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "scene/Scene.h"
#include "asset/TextureAsset.h"

using AssetTypes =
    AssetTypeList<TextureAsset, MeshAsset, GltfMaterial, Scene, EnvironmentMapAsset, SamplerAsset>;
