#pragma once

#include "asset/pbrt/PbrtMaterial.h"
#include "asset/GltfMaterial.h"

// Names are persistent type tags, independent of RTTI implementation names.
template <class F>
void forEachMaterialType(F&& visitor)
{
    visitor.template operator()<GltfMaterial>("GltfMaterial");
    visitor.template operator()<PbrtMaterial>("PbrtMaterial");
}
