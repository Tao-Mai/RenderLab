#pragma once

#include "asset/Asset.h"
#include "core/Reflect.h"
#include "ecs/Component.h"

#include <unordered_map>

namespace ecs
{
struct Render
{
    Mesh::ID meshId;
    std::unordered_map<int, Material::Desc> materialOverrides;
};

static_assert(Component<Render>);

REFLECT(Render, meshId, materialOverrides);
}
