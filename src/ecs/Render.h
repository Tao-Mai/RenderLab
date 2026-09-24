#pragma once

#include "asset/AssetId.h"
#include "asset/AssetDesc.h"
#include "core/Reflect.h"
#include "ecs/Component.h"

#include <unordered_map>

namespace ecs
{
struct Render
{
    AssetId meshId;
    std::unordered_map<int, MaterialDesc> materialOverrides;
};

static_assert(Component<Render>);

REFLECT(Render, meshId, materialOverrides);
}
