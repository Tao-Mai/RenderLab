#pragma once

#include "asset/asset_id.h"
#include "asset/asset_desc.h"
#include "core/reflect.h"
#include "ecs/component.h"

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
