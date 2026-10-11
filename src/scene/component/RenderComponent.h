#pragma once

#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "scene/Component.h"

#include <unordered_map>

struct RenderComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    [[=ReflectField{}]] MeshAsset::ID meshId;
    [[=ReflectField{}]] std::unordered_map<int, MaterialOverride> materialOverrides;
};
