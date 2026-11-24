#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/GltfMaterial.h"
#include "asset/MeshAsset.h"
#include "scene/Component.h"
#include "asset/pbrt/PbrtSurface.h"

#include <unordered_map>

struct RL_REGISTER_TYPE(Component, "RenderComponent") RenderComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    RL_FIELD("meshId") MeshAsset::ID meshId;
    RL_FIELD("materialOverrides") std::unordered_map<int, MaterialOverride> materialOverrides;
    RL_FIELD("pbrtSurface") std::optional<PbrtSurface> pbrtSurface;
};
