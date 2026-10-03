#pragma once

#include "asset/Asset.h"
#include "scene/Component.h"

#include <unordered_map>

struct RenderComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    [[=ReflectField{}]] MeshAsset::ID meshId;
    [[=ReflectField{}]] std::unordered_map<int, MaterialOverride> materialOverrides;
};
