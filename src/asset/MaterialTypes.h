#pragma once

#include "asset/OfflineMaterialAsset.h"
#include "asset/PbrMaterialAsset.h"

// Names are persistent type tags, independent of RTTI implementation names.
template <class F>
void forEachMaterialType(F&& visitor)
{
    visitor.template operator()<PbrMaterialAsset>("PbrMaterialAsset");
    visitor.template operator()<OfflineMaterialAsset>("OfflineMaterialAsset");
}
