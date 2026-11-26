#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/StandardMaterial.h"
#include "asset/TextureAsset.h"
#include "scene/Scene.h"
#include "scene/SceneLayer.h"

using AssetTypes =
    AssetTypeList<TextureAsset, MeshAsset, StandardMaterial, Scene, SceneLayer, EnvironmentMapAsset, SamplerAsset>;
