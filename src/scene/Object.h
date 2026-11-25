#pragma once

#include "asset/MeshAsset.h"
#include "scene/Transform.h"

#include <string>

struct Object
{
    std::string name;
    Transform transform;
    MeshAsset::ID meshId;
};
