#pragma once

#include "asset/Asset.h"
#include "scene/component/Component.h"

#include <unordered_map>

struct RenderComponent : Component
{
    [[=ReflectField{}]] Mesh::ID meshId;
    [[=ReflectField{}]] std::unordered_map<int, Material::Desc> materialOverrides;
};
