#pragma once

#include "asset/Asset.h"
#include "core/Reflect.h"
#include "ecs/component/Component.h"

#include <unordered_map>

namespace ecs
{
struct RenderComponent
{
    Mesh::ID meshId;
    std::unordered_map<int, Material::Desc> materialOverrides;
};

static_assert(Component<RenderComponent>);

REFLECT(RenderComponent, Render, meshId, materialOverrides);
}
